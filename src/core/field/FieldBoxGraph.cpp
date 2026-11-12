#include "FieldBoxGraph.h"
#include "FieldAst.h"
#include "FieldLex.h"
#include "FieldParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

      std::string FormatDouble(double v)
      {
         if (std::isnan(v) || std::isinf(v))
            return "0";
         if (v == 0.0)
            return "0";
         char buf[64];
         std::snprintf(buf, sizeof(buf), "%.15g", v);
         return std::string(buf);
      }

      std::string EscapeString(const std::string& s)
      {
         std::string out = "\"";
         for (char c : s)
         {
            if (c == '"' || c == '\\')
               out.push_back('\\');
            out.push_back(c);
         }
         out.push_back('"');
         return out;
      }

      int BinaryPrec(const std::string& op)
      {
         if (op == "||") return 1;
         if (op == "&&") return 2;
         if (op == "==" || op == "!=") return 3;
         if (op == "<" || op == "<=" || op == ">" || op == ">=") return 4;
         if (op == "+" || op == "-") return 5;
         if (op == "*" || op == "/" || op == "%") return 6;
         if (op == "^") return 7;
         return 0;
      }

      std::string AstExprToCode(const AstNodePtr& node, int parentPrec = 0);

      std::string AstStmtToCode(const AstNodePtr& node, const std::string& indent = "")
      {
         if (!node)
            return "";
         switch (node->kind)
         {
            case AstKind::DeclParam:
            {
               auto* p = static_cast<const AstDeclParam*>(node.get());
               std::string s = indent + "param " + p->typeName + " " + p->name + " = ";
               s += p->initExpr ? AstExprToCode(p->initExpr) : FormatDouble(p->defaultValue);
               s += " [" + FormatDouble(p->minVal) + ", " + FormatDouble(p->maxVal) + "]";
               return s;
            }
            case AstKind::DeclState:
            {
               auto* st = static_cast<const AstDeclState*>(node.get());
               std::string s = indent + "state " + st->typeName + " " + st->name;
               if (st->tableSize > 0)
                  s += "[" + std::to_string(st->tableSize) + "]";
               s += " = " + (st->initExpr ? AstExprToCode(st->initExpr) : "0");
               if (!st->boundary.empty())
                  s += " [" + st->boundary + "]";
               return s;
            }
            case AstKind::DeclAttrib:
            {
               auto* a = static_cast<const AstDeclAttrib*>(node.get());
               std::string s = indent + "attrib " + a->typeName + " " + a->name;
               if (a->initExpr)
                  s += " = " + AstExprToCode(a->initExpr);
               return s;
            }
            case AstKind::DeclOutput:
            {
               auto* o = static_cast<const AstDeclOutput*>(node.get());
               return indent + "output " + o->domainName + " " + o->typeName + " " + o->name + " = " + AstExprToCode(o->initExpr);
            }
            case AstKind::DeclInput:
            {
               auto* in = static_cast<const AstDeclInput*>(node.get());
               return indent + "input " + in->domainName + " " + in->typeName + " " + in->name;
            }
            case AstKind::Assign:
            {
               auto* a = static_cast<const AstAssign*>(node.get());
               return indent + AstExprToCode(a->lvalue) + " " + a->op + " " + AstExprToCode(a->rvalue);
            }
            case AstKind::If:
            {
               auto* iff = static_cast<const AstIf*>(node.get());
               std::string s = indent + "if (" + AstExprToCode(iff->cond) + ") {\n";
               if (iff->thenBlock && iff->thenBlock->kind == AstKind::Block)
               {
                  auto* blk = static_cast<const AstBlock*>(iff->thenBlock.get());
                  for (const auto& sub : blk->statements)
                     s += AstStmtToCode(sub, indent + "   ") + "\n";
               }
               else if (iff->thenBlock)
               {
                  s += AstStmtToCode(iff->thenBlock, indent + "   ") + "\n";
               }
               s += indent + "}";
               if (iff->elseBlock)
               {
                  s += " else {\n";
                  if (iff->elseBlock->kind == AstKind::Block)
                  {
                     auto* blk = static_cast<const AstBlock*>(iff->elseBlock.get());
                     for (const auto& sub : blk->statements)
                        s += AstStmtToCode(sub, indent + "   ") + "\n";
                  }
                  else
                  {
                     s += AstStmtToCode(iff->elseBlock, indent + "   ") + "\n";
                  }
                  s += indent + "}";
               }
               return s;
            }
            case AstKind::For:
            {
               auto* f = static_cast<const AstFor*>(node.get());
               std::string s = indent + "for (" + AstStmtToCode(f->init, "") + "; "
                             + AstExprToCode(f->cond) + "; "
                             + AstStmtToCode(f->step, "") + ") {\n";
               if (f->body && f->body->kind == AstKind::Block)
               {
                  auto* blk = static_cast<const AstBlock*>(f->body.get());
                  for (const auto& sub : blk->statements)
                     s += AstStmtToCode(sub, indent + "   ") + "\n";
               }
               s += indent + "}";
               return s;
            }
            case AstKind::Map:
            {
               auto* m = static_cast<const AstMap*>(node.get());
               std::string s = indent + "map (" + AstExprToCode(m->countExpr) + ") {\n";
               if (m->body && m->body->kind == AstKind::Block)
               {
                  auto* blk = static_cast<const AstBlock*>(m->body.get());
                  for (const auto& sub : blk->statements)
                     s += AstStmtToCode(sub, indent + "   ") + "\n";
               }
               s += indent + "}";
               return s;
            }
            case AstKind::Block:
            {
               auto* blk = static_cast<const AstBlock*>(node.get());
               std::string s;
               for (size_t i = 0; i < blk->statements.size(); ++i)
               {
                  if (i > 0)
                     s += "\n";
                  s += AstStmtToCode(blk->statements[i], indent);
               }
               return s;
            }
            default:
               return indent + AstExprToCode(node);
         }
      }

      std::string AstExprToCode(const AstNodePtr& node, int parentPrec)
      {
         if (!node)
            return "0";
         switch (node->kind)
         {
            case AstKind::Literal:
            {
               auto* lit = static_cast<const AstLiteral*>(node.get());
               if (lit->isString)
                  return EscapeString(lit->stringValue);
               if (lit->isBool)
                  return lit->boolValue ? "true" : "false";
               return FormatDouble(lit->numberValue);
            }
            case AstKind::Ident:
               return static_cast<const AstIdent*>(node.get())->name;
            case AstKind::Access:
            {
               auto* acc = static_cast<const AstAccess*>(node.get());
               return AstExprToCode(acc->base, 10) + "." + acc->field;
            }
            case AstKind::Index:
            {
               auto* idx = static_cast<const AstIndex*>(node.get());
               return AstExprToCode(idx->base, 10) + "[" + AstExprToCode(idx->index, 0) + "]";
            }
            case AstKind::Unary:
            {
               auto* un = static_cast<const AstUnary*>(node.get());
               std::string inner = AstExprToCode(un->operand, 8);
               std::string s = un->op + inner;
               if (parentPrec > 8)
                  return "(" + s + ")";
               return s;
            }
            case AstKind::Binary:
            {
               auto* bin = static_cast<const AstBinary*>(node.get());
               int prec = BinaryPrec(bin->op);
               std::string lhs = AstExprToCode(bin->lhs, prec);
               std::string rhs = AstExprToCode(bin->rhs, prec + 1);
               std::string s = lhs + " " + bin->op + " " + rhs;
               if (prec <= parentPrec)
                  return "(" + s + ")";
               return s;
            }
            case AstKind::Call:
            {
               auto* call = static_cast<const AstCall*>(node.get());
               std::string s = call->callee + "(";
               for (size_t i = 0; i < call->args.size(); ++i)
               {
                  if (i > 0)
                     s += ", ";
                  s += AstExprToCode(call->args[i], 0);
               }
               s += ")";
               return s;
            }
            default:
               return "0";
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

      // Collects variables whose bare name is passed to reduce.*(var) or are state/table/reassigned.
      struct PreScanInfo
      {
         std::unordered_set<std::string> stateNames;
         std::unordered_set<std::string> paramNames;
         std::unordered_set<std::string> attribNames;
         std::unordered_set<std::string> inputNames;
         std::unordered_set<std::string> reduceArgNames;
         std::unordered_map<std::string, int> assignCounts;
      };

      void PreScanExpr(const AstNodePtr& node, PreScanInfo& info)
      {
         if (!node)
            return;
         switch (node->kind)
         {
            case AstKind::Unary:
               PreScanExpr(static_cast<const AstUnary*>(node.get())->operand, info);
               break;
            case AstKind::Binary:
               PreScanExpr(static_cast<const AstBinary*>(node.get())->lhs, info);
               PreScanExpr(static_cast<const AstBinary*>(node.get())->rhs, info);
               break;
            case AstKind::Access:
               PreScanExpr(static_cast<const AstAccess*>(node.get())->base, info);
               break;
            case AstKind::Index:
               PreScanExpr(static_cast<const AstIndex*>(node.get())->base, info);
               PreScanExpr(static_cast<const AstIndex*>(node.get())->index, info);
               break;
            case AstKind::Call:
            {
               auto* c = static_cast<const AstCall*>(node.get());
               if (c->callee.rfind("reduce.", 0) == 0 && !c->args.empty() && c->args[0]->kind == AstKind::Ident)
                  info.reduceArgNames.insert(static_cast<const AstIdent*>(c->args[0].get())->name);
               for (const auto& a : c->args)
                  PreScanExpr(a, info);
               break;
            }
            default:
               break;
         }
      }

      void PreScanStmt(const AstNodePtr& node, PreScanInfo& info)
      {
         if (!node)
            return;
         switch (node->kind)
         {
            case AstKind::Program:
               for (const auto& s : static_cast<const AstProgram*>(node.get())->statements)
                  PreScanStmt(s, info);
               break;
            case AstKind::Block:
               for (const auto& s : static_cast<const AstBlock*>(node.get())->statements)
                  PreScanStmt(s, info);
               break;
            case AstKind::DeclState:
               info.stateNames.insert(static_cast<const AstDeclState*>(node.get())->name);
               break;
            case AstKind::DeclParam:
               info.paramNames.insert(static_cast<const AstDeclParam*>(node.get())->name);
               break;
            case AstKind::DeclAttrib:
               info.attribNames.insert(static_cast<const AstDeclAttrib*>(node.get())->name);
               break;
            case AstKind::DeclInput:
               info.inputNames.insert(static_cast<const AstDeclInput*>(node.get())->name);
               break;
            case AstKind::DeclOutput:
               PreScanExpr(static_cast<const AstDeclOutput*>(node.get())->initExpr, info);
               break;
            case AstKind::Assign:
            {
               auto* a = static_cast<const AstAssign*>(node.get());
               if (a->lvalue && a->lvalue->kind == AstKind::Ident)
                  info.assignCounts[static_cast<const AstIdent*>(a->lvalue.get())->name]++;
               else if (a->lvalue && a->lvalue->kind == AstKind::Access &&
                        static_cast<const AstAccess*>(a->lvalue.get())->base->kind == AstKind::Ident)
                  info.assignCounts[static_cast<const AstIdent*>(static_cast<const AstAccess*>(a->lvalue.get())->base.get())->name]++;
               PreScanExpr(a->rvalue, info);
               break;
            }
            case AstKind::If:
            {
               auto* iff = static_cast<const AstIf*>(node.get());
               PreScanExpr(iff->cond, info);
               PreScanStmt(iff->thenBlock, info);
               PreScanStmt(iff->elseBlock, info);
               break;
            }
            case AstKind::For:
            {
               auto* f = static_cast<const AstFor*>(node.get());
               PreScanStmt(f->init, info);
               PreScanExpr(f->cond, info);
               PreScanStmt(f->step, info);
               PreScanStmt(f->body, info);
               break;
            }
            case AstKind::Map:
            {
               auto* m = static_cast<const AstMap*>(node.get());
               PreScanExpr(m->countExpr, info);
               PreScanStmt(m->body, info);
               break;
            }
            default:
               PreScanExpr(node, info);
               break;
         }
      }

      // Checks if an expression contains constructs that cannot be represented as pure boxes
      // (state offset read `A(uv+d)`, table indexing `tbl[i]`, string literals, delay()).
      bool ExprNeedsCodeBox(const AstNodePtr& node, const PreScanInfo& info)
      {
         if (!node)
            return false;
         switch (node->kind)
         {
            case AstKind::Literal:
               return static_cast<const AstLiteral*>(node.get())->isString;
            case AstKind::Index:
               return true;
            case AstKind::Unary:
               return ExprNeedsCodeBox(static_cast<const AstUnary*>(node.get())->operand, info);
            case AstKind::Binary:
               return ExprNeedsCodeBox(static_cast<const AstBinary*>(node.get())->lhs, info)
                   || ExprNeedsCodeBox(static_cast<const AstBinary*>(node.get())->rhs, info);
            case AstKind::Access:
               return ExprNeedsCodeBox(static_cast<const AstAccess*>(node.get())->base, info);
            case AstKind::Call:
            {
               auto* c = static_cast<const AstCall*>(node.get());
               if (info.stateNames.count(c->callee) > 0)
                  return true; // Pixel state offset read A(uv + d)
               if (c->callee == "delay" || c->callee == "phase" || c->callee == "vec4" ||
                   c->callee == "emit" || c->callee == "connect" || c->callee == "set" || c->callee == "place")
                  return true;
               for (const auto& a : c->args)
               {
                  if (ExprNeedsCodeBox(a, info))
                     return true;
               }
               return false;
            }
            default:
               return false;
         }
      }

      struct OutRef
      {
         int boxId = 0;
         int pinIdx = 0;
         ValueKind kind = ValueKind::Number;
      };

      struct GraphBuilder
      {
         BoxGraph& graph;
         const PreScanInfo& info;
         int nextBoxId = 1;
         int nextWireId = 1;
         int nextGroupId = 1;
         int nextStmtOrder = 0;
         std::unordered_map<std::string, OutRef> env;
         std::unordered_map<std::string, OutRef> builtinSources;

         BoxNode& AddBox(const std::string& specId, const std::string& customLabel = "")
         {
            BoxNode node;
            node.id = nextBoxId++;
            node.key = "b" + std::to_string(node.id);
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
            if (spIt != kBuiltinSpec.end())
            {
               BoxNode& b = AddBox(spIt->second);
               b.key = "src_" + name;
               b.varName = name;
               OutRef ref{ b.id, 0, b.outlets.empty() ? ValueKind::Number : b.outlets[0].kind };
               builtinSources[name] = ref;
               return ref;
            }

            BoxNode& b = AddBox("ident", name);
            b.key = "src_" + name;
            b.varName = name;
            b.outlets.push_back({ name, ValueKind::Number, 0.0 });
            OutRef ref{ b.id, 0, ValueKind::Number };
            builtinSources[name] = ref;
            return ref;
         }

         OutRef LowerExpr(const AstNodePtr& node)
         {
            if (!node)
            {
               BoxNode& b = AddBox("number", "0");
               b.numberValue = 0.0;
               b.literalText = "0";
               return { b.id, 0, ValueKind::Number };
            }

            switch (node->kind)
            {
               case AstKind::Literal:
               {
                  auto* lit = static_cast<const AstLiteral*>(node.get());
                  std::string txt = lit->isBool ? (lit->boolValue ? "1" : "0") : FormatDouble(lit->numberValue);
                  BoxNode& b = AddBox("number", txt);
                  b.numberValue = lit->numberValue;
                  b.literalText = txt;
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
                  // Single dynamic outlet for the requested swizzle if not just x/y/z
                  if (acc->field != "x" && acc->field != "y" && acc->field != "z")
                  {
                     sw.outlets.clear();
                     sw.outlets.push_back({ acc->field, acc->field.size() > 1 ? ValueKind::Vec3 : ValueKind::Number, 0.0 });
                     Connect(baseRef.boxId, baseRef.pinIdx, sw.id, 0, baseRef.kind);
                     return { sw.id, 0, sw.outlets[0].kind };
                  }
                  int outIdx = (acc->field == "x") ? 0 : (acc->field == "y" ? 1 : 2);
                  Connect(baseRef.boxId, baseRef.pinIdx, sw.id, 0, baseRef.kind);
                  return { sw.id, outIdx, ValueKind::Number };
               }

               case AstKind::Unary:
               {
                  auto* un = static_cast<const AstUnary*>(node.get());
                  // Sugar recognition: !(a && b) -> nand, !(a || b) -> nor
                  if (un->op == "!" && un->operand && un->operand->kind == AstKind::Binary)
                  {
                     auto* inner = static_cast<const AstBinary*>(un->operand.get());
                     if (inner->op == "&&" || inner->op == "||")
                     {
                        OutRef a = LowerExpr(inner->lhs);
                        OutRef b = LowerExpr(inner->rhs);
                        BoxNode& bx = AddBox(inner->op == "&&" ? "nand" : "nor");
                        Connect(a.boxId, a.pinIdx, bx.id, 0, ValueKind::Bool);
                        Connect(b.boxId, b.pinIdx, bx.id, 1, ValueKind::Bool);
                        return { bx.id, 0, ValueKind::Bool };
                     }
                  }
                  OutRef opRef = LowerExpr(un->operand);
                  std::string specId = (un->op == "!") ? "not" : "neg";
                  BoxNode& bx = AddBox(specId);
                  Connect(opRef.boxId, opRef.pinIdx, bx.id, 0, opRef.kind);
                  return { bx.id, 0, (un->op == "!") ? ValueKind::Bool : opRef.kind };
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
                     bool boolSugar = IsBoolLikeExpr(bin->lhs) && IsBoolLikeExpr(bin->rhs);
                     specId = boolSugar ? "xnor" : "eq";
                     outKind = ValueKind::Bool;
                  }
                  else if (bin->op == "!=")
                  {
                     bool boolSugar = IsBoolLikeExpr(bin->lhs) || IsBoolLikeExpr(bin->rhs);
                     specId = boolSugar ? "xor" : "ne";
                     outKind = ValueKind::Bool;
                  }
                  else if (bin->op == "&&") { specId = "and"; outKind = ValueKind::Bool; }
                  else if (bin->op == "||") { specId = "or"; outKind = ValueKind::Bool; }

                  BoxNode& bx = AddBox(specId);
                  Connect(lhs.boxId, lhs.pinIdx, bx.id, 0, lhs.kind);
                  Connect(rhs.boxId, rhs.pinIdx, bx.id, 1, rhs.kind);
                  return { bx.id, 0, outKind };
               }

               case AstKind::Call:
               {
                  auto* call = static_cast<const AstCall*>(node.get());

                  // Neighbour read: X.at(idx)
                  if (call->callee.size() > 3 && call->callee.compare(call->callee.size() - 3, 3, ".at") == 0)
                  {
                     std::string baseName = call->callee.substr(0, call->callee.size() - 3);
                     BoxNode& bx = AddBox("neighbour", baseName + ".at");
                     bx.varName = baseName;
                     if (!call->args.empty())
                     {
                        OutRef idxRef = LowerExpr(call->args[0]);
                        Connect(idxRef.boxId, idxRef.pinIdx, bx.id, 0, idxRef.kind);
                     }
                     return { bx.id, 0, ValueKind::Vec3 };
                  }

                  // Reduce: reduce.max(x), reduce.mean(x), etc.
                  if (call->callee.rfind("reduce.", 0) == 0)
                  {
                     std::string op = call->callee.substr(7);
                     BoxNode& bx = AddBox("reduce", call->callee);
                     bx.swizzleField = op;
                     if (!call->args.empty())
                     {
                        OutRef argRef = LowerExpr(call->args[0]);
                        Connect(argRef.boxId, argRef.pinIdx, bx.id, 0, argRef.kind);
                     }
                     return { bx.id, 0, ValueKind::Number };
                  }

                  // Special 0-arg or variable-arg functions
                  std::string specId = call->callee;
                  if (const BoxSpec* spec = FindBoxSpec(specId))
                  {
                     int bxId = 0;
                     size_t nInlets = 0;
                     ValueKind outKind = ValueKind::Number;
                     {
                        BoxNode& bx = AddBox(specId);
                        bxId = bx.id;
                        nInlets = bx.inlets.size();
                        outKind = bx.outlets.empty() ? ValueKind::Number : bx.outlets[0].kind;
                     }
                     size_t nArgs = std::min(call->args.size(), nInlets);
                     for (size_t i = 0; i < nArgs; ++i)
                     {
                        OutRef argRef = LowerExpr(call->args[i]);
                        Connect(argRef.boxId, argRef.pinIdx, bxId, static_cast<int>(i), argRef.kind);
                     }
                     // Handle extra args on rand(lo, hi, rate, seed) if any
                     for (size_t i = nArgs; i < call->args.size(); ++i)
                     {
                        if (BoxNode* bx = graph.FindBox(bxId))
                           bx->inlets.push_back({ "arg" + std::to_string(i), ValueKind::Number, 0.0 });
                        OutRef argRef = LowerExpr(call->args[i]);
                        Connect(argRef.boxId, argRef.pinIdx, bxId, static_cast<int>(i), argRef.kind);
                     }
                     return { bxId, 0, outKind };
                  }

                  // Fallback generic call box
                  BoxNode& bx = AddBox("code", call->callee);
                  bx.rawCode = AstExprToCode(node);
                  bx.outlets.push_back({ "out", ValueKind::Number, 0.0 });
                  return { bx.id, 0, ValueKind::Number };
               }

               default:
               {
                  BoxNode& bx = AddBox("code");
                  bx.rawCode = AstExprToCode(node);
                  bx.outlets.push_back({ "out", ValueKind::Number, 0.0 });
                  return { bx.id, 0, ValueKind::Number };
               }
            }
         }

         void AddCodeBoxStmt(const AstNodePtr& stmt, int whenGroupId)
         {
            BoxNode& bx = AddBox("code");
            bx.rawCode = AstStmtToCode(stmt);
            bx.stmtOrder = nextStmtOrder++;
            bx.whenGroupId = whenGroupId;
         }

         void LowerStmt(const AstNodePtr& stmt, int whenGroupId = -1)
         {
            if (!stmt)
               return;

            switch (stmt->kind)
            {
               case AstKind::DeclParam:
               {
                  auto* p = static_cast<const AstDeclParam*>(stmt.get());
                  BoxNode& bx = AddBox("knob", p->name);
                  bx.key = "k_" + p->name;
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
                  if (st->tableSize > 0)
                  {
                     AddCodeBoxStmt(stmt, whenGroupId);
                     break;
                  }
                  int bxId = 0;
                  ValueKind k = (st->typeName == "vec3") ? ValueKind::Vec3 : ValueKind::Number;
                  {
                     BoxNode& bx = AddBox("previous", st->name);
                     bxId = bx.id;
                     bx.key = "s_" + st->name;
                     bx.varName = st->name;
                     bx.varType = st->typeName;
                     bx.boundary = st->boundary;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     bx.outlets[0].kind = k;
                  }
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
                  int bxId = 0;
                  {
                     BoxNode& bx = AddBox("attrib", a->name);
                     bxId = bx.id;
                     bx.key = "a_" + a->name;
                     bx.varName = a->name;
                     bx.varType = a->typeName;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                  }
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
                  if (ExprNeedsCodeBox(o->initExpr, info))
                  {
                     AddCodeBoxStmt(stmt, whenGroupId);
                     break;
                  }
                  int bxId = 0;
                  {
                     BoxNode& bx = AddBox("output_pin", o->name);
                     bxId = bx.id;
                     bx.key = "out_" + o->name;
                     bx.varName = o->name;
                     bx.varType = o->typeName;
                     bx.domainName = o->domainName;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     bx.inlets.push_back({ o->name, ValueKind::Number, 0.0 });
                  }
                  OutRef valRef = LowerExpr(o->initExpr);
                  Connect(valRef.boxId, valRef.pinIdx, bxId, 0, valRef.kind);
                  break;
               }

               case AstKind::DeclInput:
               {
                  auto* in = static_cast<const AstDeclInput*>(stmt.get());
                  BoxNode& bx = AddBox("input_pin", in->name);
                  bx.key = "in_" + in->name;
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
               {
                  auto* a = static_cast<const AstAssign*>(stmt.get());
                  if (ExprNeedsCodeBox(a->rvalue, info) ||
                      (a->lvalue && a->lvalue->kind == AstKind::Index))
                  {
                     AddCodeBoxStmt(stmt, whenGroupId);
                     break;
                  }

                  std::string targetBase;
                  std::string targetField;
                  if (a->lvalue && a->lvalue->kind == AstKind::Ident)
                  {
                     targetBase = static_cast<const AstIdent*>(a->lvalue.get())->name;
                  }
                  else if (a->lvalue && a->lvalue->kind == AstKind::Access)
                  {
                     auto* acc = static_cast<const AstAccess*>(a->lvalue.get());
                     if (acc->base && acc->base->kind == AstKind::Ident)
                     {
                        targetBase = static_cast<const AstIdent*>(acc->base.get())->name;
                        targetField = acc->field;
                     }
                  }

                  if (targetBase.empty())
                  {
                     AddCodeBoxStmt(stmt, whenGroupId);
                     break;
                  }

                  OutRef rhsRef = LowerExpr(a->rvalue);

                  // Built-in outlets: P, N, Cd, publish
                  if (targetBase == "P")
                  {
                     BoxNode& bx = AddBox("position_out");
                     bx.varName = "P";
                     bx.assignOp = a->op;
                     bx.swizzleField = targetField;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     int pinIdx = 3; // xyz
                     if (targetField == "x") pinIdx = 0;
                     else if (targetField == "y") pinIdx = 1;
                     else if (targetField == "z") pinIdx = 2;
                     else if (!targetField.empty())
                     {
                        // Multi-char swizzle like P.xz: use code box for safety
                        graph.boxes.pop_back();
                        AddCodeBoxStmt(stmt, whenGroupId);
                        break;
                     }
                     Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, pinIdx, rhsRef.kind);
                     break;
                  }
                  if (targetBase == "N" && targetField.empty() && a->op == "=")
                  {
                     BoxNode& bx = AddBox("normal_out");
                     bx.varName = "N";
                     bx.assignOp = a->op;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, 0, rhsRef.kind);
                     break;
                  }
                  if (targetBase == "Cd" && targetField.empty() && a->op == "=")
                  {
                     BoxNode& bx = AddBox("color_out");
                     bx.varName = "Cd";
                     bx.assignOp = a->op;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, 0, rhsRef.kind);
                     break;
                  }
                  if (targetBase == "publish" && targetField.empty() && a->op == "=")
                  {
                     BoxNode& bx = AddBox(rhsRef.kind == ValueKind::Pulse ? "pulse_out" : "value_out");
                     bx.varName = "publish";
                     bx.assignOp = a->op;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, 0, rhsRef.kind);
                     break;
                  }

                  // State write or compound assignment or reassigned variable -> explicit assign box
                  bool isState = info.stateNames.count(targetBase) > 0;
                  bool isReassigned = false;
                  auto cntIt = info.assignCounts.find(targetBase);
                  if (cntIt != info.assignCounts.end() && cntIt->second > 1)
                     isReassigned = true;

                  if (isState || !targetField.empty() || a->op != "=" || isReassigned || whenGroupId >= 0)
                  {
                     BoxNode& bx = AddBox("assign_var", targetBase + (targetField.empty() ? "" : "." + targetField) + " " + a->op);
                     bx.varName = targetBase;
                     bx.swizzleField = targetField;
                     bx.assignOp = a->op;
                     bx.stmtOrder = nextStmtOrder++;
                     bx.whenGroupId = whenGroupId;
                     bx.inlets.push_back({ "val", rhsRef.kind, 0.0 });
                     bx.outlets.push_back({ targetBase, rhsRef.kind, 0.0 });
                     Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, 0, rhsRef.kind);
                     if (!isState && targetField.empty() && a->op == "=")
                        env[targetBase] = { bx.id, 0, rhsRef.kind };
                     break;
                  }

                  // Single-assignment pure/named wire: attach wireName to the producer box
                  if (BoxNode* prod = graph.FindBox(rhsRef.boxId))
                  {
                     if (prod->wireName.empty() && prod->stmtOrder < 0)
                     {
                        prod->wireName = targetBase;
                        prod->stmtOrder = nextStmtOrder++;
                        env[targetBase] = rhsRef;
                        break;
                     }
                  }

                  // If producer already has a wireName or is a source/param/state, create an alias assign_var
                  BoxNode& bx = AddBox("assign_var", targetBase + " =");
                  bx.varName = targetBase;
                  bx.assignOp = "=";
                  bx.stmtOrder = nextStmtOrder++;
                  bx.whenGroupId = whenGroupId;
                  bx.inlets.push_back({ "val", rhsRef.kind, 0.0 });
                  bx.outlets.push_back({ targetBase, rhsRef.kind, 0.0 });
                  Connect(rhsRef.boxId, rhsRef.pinIdx, bx.id, 0, rhsRef.kind);
                  env[targetBase] = { bx.id, 0, rhsRef.kind };
                  break;
               }

               case AstKind::If:
               {
                  auto* iff = static_cast<const AstIf*>(stmt.get());
                  if (iff->elseBlock || ExprNeedsCodeBox(iff->cond, info))
                  {
                     AddCodeBoxStmt(stmt, whenGroupId);
                     break;
                  }

                  std::vector<AstNodePtr> thenStmts;
                  if (iff->thenBlock && iff->thenBlock->kind == AstKind::Block)
                     thenStmts = static_cast<const AstBlock*>(iff->thenBlock.get())->statements;
                  else if (iff->thenBlock)
                     thenStmts.push_back(iff->thenBlock);

                  // Check if nested if/for/map exists inside thenStmts
                  bool hasNestedControl = false;
                  for (const auto& s : thenStmts)
                  {
                     if (s && (s->kind == AstKind::If || s->kind == AstKind::For || s->kind == AstKind::Map))
                     {
                        hasNestedControl = true;
                        break;
                     }
                  }
                  if (hasNestedControl || whenGroupId >= 0)
                  {
                     AddCodeBoxStmt(stmt, whenGroupId);
                     break;
                  }

                  OutRef condRef = LowerExpr(iff->cond);

                  // Check if thenStmts is ONLY note(...) calls
                  bool onlyNotes = !thenStmts.empty();
                  for (const auto& s : thenStmts)
                  {
                     if (!s || s->kind != AstKind::Call || static_cast<const AstCall*>(s.get())->callee != "note")
                     {
                        onlyNotes = false;
                        break;
                     }
                  }

                  if (onlyNotes)
                  {
                     for (const auto& s : thenStmts)
                     {
                        auto* call = static_cast<const AstCall*>(s.get());
                        int pnId = 0;
                        {
                           BoxNode& pn = AddBox("play_note");
                           pnId = pn.id;
                           pn.stmtOrder = nextStmtOrder++;
                        }
                        Connect(condRef.boxId, condRef.pinIdx, pnId, 0, ValueKind::Pulse);
                        for (size_t i = 0; i < std::min<size_t>(call->args.size(), 3); ++i)
                        {
                           OutRef aRef = LowerExpr(call->args[i]);
                           Connect(aRef.boxId, aRef.pinIdx, pnId, static_cast<int>(i + 1), aRef.kind);
                        }
                     }
                     break;
                  }

                  // Mixed statements -> when_group
                  BoxGroup grp;
                  grp.id = nextGroupId++;
                  grp.name = "when";
                  grp.isWhenGroup = true;
                  grp.condBoxId = condRef.boxId;
                  grp.condPinIdx = condRef.pinIdx;
                  grp.stmtOrder = nextStmtOrder++;
                  graph.groups.push_back(grp);
                  size_t grpIdx = graph.groups.size() - 1;
                  int gid = grp.id;

                  size_t boxStart = graph.boxes.size();
                  for (const auto& s : thenStmts)
                  {
                     if (s && s->kind == AstKind::Call && static_cast<const AstCall*>(s.get())->callee == "note")
                     {
                        auto* call = static_cast<const AstCall*>(s.get());
                        int pnId = 0;
                        {
                           BoxNode& pn = AddBox("play_note");
                           pnId = pn.id;
                           pn.stmtOrder = nextStmtOrder++;
                           pn.whenGroupId = gid;
                        }
                        for (size_t i = 0; i < std::min<size_t>(call->args.size(), 3); ++i)
                        {
                           OutRef aRef = LowerExpr(call->args[i]);
                           Connect(aRef.boxId, aRef.pinIdx, pnId, static_cast<int>(i + 1), aRef.kind);
                        }
                     }
                     else
                     {
                        LowerStmt(s, gid);
                     }
                  }
                  for (size_t bi = boxStart; bi < graph.boxes.size(); ++bi)
                  {
                     if (graph.boxes[bi].whenGroupId == gid)
                        graph.groups[grpIdx].boxIds.push_back(graph.boxes[bi].id);
                  }
                  break;
               }

               case AstKind::Call:
               {
                  auto* call = static_cast<const AstCall*>(stmt.get());
                  if (call->callee == "note" && !ExprNeedsCodeBox(stmt, info))
                  {
                     int pnId = 0;
                     {
                        BoxNode& pn = AddBox("play_note");
                        pnId = pn.id;
                        pn.stmtOrder = nextStmtOrder++;
                        pn.whenGroupId = whenGroupId;
                     }
                     for (size_t i = 0; i < std::min<size_t>(call->args.size(), 3); ++i)
                     {
                        OutRef aRef = LowerExpr(call->args[i]);
                        Connect(aRef.boxId, aRef.pinIdx, pnId, static_cast<int>(i + 1), aRef.kind);
                     }
                     break;
                  }
                  AddCodeBoxStmt(stmt, whenGroupId);
                  break;
               }

               default:
                  AddCodeBoxStmt(stmt, whenGroupId);
                  break;
            }
         }
      };
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
      std::vector<Token> tokens;
      if (!Lex(code, tokens, outError))
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
      PreScanStmt(ast, preScan);

      GraphBuilder builder{ outGraph, preScan };
      if (ast && ast->kind == AstKind::Program)
      {
         const auto* prog = static_cast<const AstProgram*>(ast.get());
         for (const AstNodePtr& stmt : prog->statements)
            builder.LowerStmt(stmt);
      }

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

   namespace
   {
      struct ExprPrinter
      {
         const BoxGraph& graph;
         std::unordered_map<int, int> readerCount;
         std::unordered_map<int, std::string> boundVar;
         std::unordered_set<int> emittedStmts;
         int tempCounter = 1;

         ExprPrinter(const BoxGraph& g) : graph(g)
         {
            for (const BoxWire& w : graph.wires)
               readerCount[w.srcBoxId]++;
            for (const BoxNode& b : graph.boxes)
            {
               if (!b.wireName.empty())
                  boundVar[b.id] = b.wireName;
               else if (b.specId == "knob" || b.specId == "previous" || b.specId == "attrib" ||
                        b.specId == "input_pin" || b.specId == "assign_var")
                  boundVar[b.id] = b.varName;
               else if (b.specId == "ident")
                  boundVar[b.id] = b.varName;
            }
         }

         std::string PrintInlet(int dstBoxId, int dstPinIdx, const std::string& fallback = "0", int parentPrec = 0)
         {
            const BoxWire* w = graph.FindWireTo(dstBoxId, dstPinIdx);
            if (!w)
               return fallback;
            return PrintBoxExpr(w->srcBoxId, w->srcPinIdx, parentPrec);
         }

         std::string PrintBoxExpr(int boxId, int pinIdx, int parentPrec = 0)
         {
            const BoxNode* b = graph.FindBox(boxId);
            if (!b)
               return "0";

            // Builtin sources
            if (b->specId == "time") return "t";
            if (b->specId == "dt") return "dt";
            if (b->specId == "age") return "age";
            if (b->specId == "beat") return "beat";
            if (b->specId == "index") return "i";
            if (b->specId == "count") return "count";
            if (b->specId == "position") return "P";
            if (b->specId == "normal") return "N";
            if (b->specId == "color_in") return "Cd";
            if (b->specId == "uv") return "uv";
            if (b->specId == "audio_in") return "in";
            if (b->specId == "sr") return "sr";
            if (b->specId == "freq") return "freq";
            if (b->specId == "gate") return "gate";
            if (b->specId == "noteOn") return "noteOn";
            if (b->specId == "noteNum") return "noteNum";
            if (b->specId == "noteVel") return "noteVel";
            if (b->specId == "ident") return b->varName;

            if (b->specId == "knob" || b->specId == "previous" || b->specId == "attrib" ||
                b->specId == "input_pin" || b->specId == "assign_var")
               return b->varName;

            if (!b->wireName.empty() && emittedStmts.count(b->id) > 0)
               return b->wireName;

            auto bvIt = boundVar.find(b->id);
            if (bvIt != boundVar.end() && emittedStmts.count(b->id) > 0)
               return bvIt->second;

            return PrintRawBoxExpr(*b, pinIdx, parentPrec);
         }

         std::string PrintRawBoxExpr(const BoxNode& b, int pinIdx, int parentPrec = 0)
         {
            if (b.specId == "number")
               return b.literalText.empty() ? FormatDouble(b.numberValue) : b.literalText;

            if (b.specId == "code")
               return b.rawCode.empty() ? "0" : b.rawCode;

            if (b.specId == "swizzle")
            {
               std::string field = b.swizzleField;
               if (field.empty())
                  field = (pinIdx == 0) ? "x" : (pinIdx == 1 ? "y" : "z");
               return PrintInlet(b.id, 0, "P", 10) + "." + field;
            }

            if (b.specId == "neighbour")
               return b.varName + ".at(" + PrintInlet(b.id, 0, "i", 0) + ")";

            if (b.specId == "reduce")
               return "reduce." + (b.swizzleField.empty() ? "max" : b.swizzleField) + "(" + PrintInlet(b.id, 0, "0", 0) + ")";

            if (b.specId == "neg" || b.specId == "not")
            {
               std::string op = (b.specId == "not") ? "!" : "-";
               std::string s = op + PrintInlet(b.id, 0, "0", 8);
               return parentPrec > 8 ? "(" + s + ")" : s;
            }

            if (b.specId == "nand" || b.specId == "nor")
            {
               std::string op = (b.specId == "nand") ? "&&" : "||";
               std::string s = "!(" + PrintInlet(b.id, 0, "0", 2) + " " + op + " " + PrintInlet(b.id, 1, "0", 2) + ")";
               return parentPrec > 8 ? "(" + s + ")" : s;
            }

            static const std::unordered_map<std::string, std::string> kBinOps = {
               { "add", "+" }, { "sub", "-" }, { "mul", "*" }, { "div", "/" },
               { "mod", "%" }, { "pow", "^" }, { "lt", "<" }, { "le", "<=" },
               { "gt", ">" }, { "ge", ">=" }, { "eq", "==" }, { "ne", "!=" },
               { "xor", "!=" }, { "xnor", "==" }, { "and", "&&" }, { "or", "||" }
            };
            auto binIt = kBinOps.find(b.specId);
            if (binIt != kBinOps.end())
            {
               const std::string& op = binIt->second;
               int prec = BinaryPrec(op);
               std::string lhs = PrintInlet(b.id, 0, "0", prec);
               std::string rhs = PrintInlet(b.id, 1, "0", prec + 1);
               std::string s = lhs + " " + op + " " + rhs;
               return prec <= parentPrec ? "(" + s + ")" : s;
            }

            // Sugar functions
            if (b.specId == "rescale")
            {
               std::string x = PrintInlet(b.id, 0, "0", 5);
               std::string inMin = PrintInlet(b.id, 1, "0", 5);
               std::string inMax = PrintInlet(b.id, 2, "1", 5);
               std::string outMin = PrintInlet(b.id, 3, "0", 5);
               std::string outMax = PrintInlet(b.id, 4, "1", 5);
               return "mix(" + outMin + ", " + outMax + ", (" + x + " - " + inMin + ") / max(" + inMax + " - " + inMin + ", 0.0001))";
            }

            // Standard function call
            const BoxSpec* spec = FindBoxSpec(b.specId);
            std::string fn = spec ? spec->fieldWord : b.specId;
            std::string s = fn + "(";
            for (size_t i = 0; i < b.inlets.size(); ++i)
            {
               if (i > 0)
                  s += ", ";
               s += PrintInlet(b.id, static_cast<int>(i), FormatDouble(b.inlets[i].defaultValue), 0);
            }
            s += ")";
            return s;
         }

         std::string PrintStmt(const BoxNode& b, const std::string& indent = "")
         {
            if (b.specId == "code")
               return indent + b.rawCode;

            if (b.specId == "knob")
               return indent + "param " + (b.varType.empty() ? "float" : b.varType) + " " + b.varName +
                      " = " + FormatDouble(b.numberValue) + " [" + FormatDouble(b.minVal) + ", " + FormatDouble(b.maxVal) + "]";

            if (b.specId == "previous")
            {
               std::string s = indent + "state " + (b.varType.empty() ? "float" : b.varType) + " " + b.varName +
                               " = " + PrintInlet(b.id, 1, "0", 0);
               if (!b.boundary.empty())
                  s += " [" + b.boundary + "]";
               return s;
            }

            if (b.specId == "attrib")
            {
               std::string s = indent + "attrib " + (b.varType.empty() ? "float" : b.varType) + " " + b.varName;
               if (graph.FindWireTo(b.id, 0))
                  s += " = " + PrintInlet(b.id, 0, "0", 0);
               return s;
            }

            if (b.specId == "output_pin")
               return indent + "output " + b.domainName + " " + b.varType + " " + b.varName + " = " + PrintInlet(b.id, 0, "0", 0);

            if (b.specId == "input_pin")
               return indent + "input " + b.domainName + " " + b.varType + " " + b.varName;

            if (b.specId == "position_out")
            {
               std::string lhs = b.swizzleField.empty() ? "P" : ("P." + b.swizzleField);
               int pinIdx = (b.swizzleField == "x") ? 0 : (b.swizzleField == "y" ? 1 : (b.swizzleField == "z" ? 2 : 3));
               return indent + lhs + " " + b.assignOp + " " + PrintInlet(b.id, pinIdx, "0", 0);
            }

            if (b.specId == "normal_out")
               return indent + "N " + b.assignOp + " " + PrintInlet(b.id, 0, "vec3(0, 1, 0)", 0);

            if (b.specId == "color_out")
               return indent + "Cd " + b.assignOp + " " + PrintInlet(b.id, 0, "vec3(1, 1, 1)", 0);

            if (b.specId == "value_out" || b.specId == "pulse_out")
               return indent + "publish " + b.assignOp + " " + PrintInlet(b.id, 0, "0", 0);

            if (b.specId == "assign_var")
            {
               std::string lhs = b.varName + (b.swizzleField.empty() ? "" : ("." + b.swizzleField));
               emittedStmts.insert(b.id);
               return indent + lhs + " " + b.assignOp + " " + PrintInlet(b.id, 0, "0", 0);
            }

            if (b.specId == "play_note")
            {
               std::string noteCall = "note(" + PrintInlet(b.id, 1, "60", 0) + ", " +
                                      PrintInlet(b.id, 2, "0.8", 0) + ", " +
                                      PrintInlet(b.id, 3, "0.25", 0) + ")";
               if (b.whenGroupId < 0 && graph.FindWireTo(b.id, 0))
                  return indent + "if (" + PrintInlet(b.id, 0, "1", 0) + ") {\n" + indent + "   " + noteCall + "\n" + indent + "}";
               return indent + noteCall;
            }

            if (!b.wireName.empty())
            {
               std::string rhs = PrintRawBoxExpr(b, 0, 0);
               emittedStmts.insert(b.id);
               return indent + b.wireName + " = " + rhs;
            }

            return "";
         }
      };
   }

   std::string PrintBoxGraphToCode(const BoxGraph& graph, bool includeHeader)
   {
      ExprPrinter printer(graph);
      std::ostringstream out;
      if (includeHeader)
         out << FormatBoxesHeader(graph) << "\n";

      // Collect ordered top-level items: either standalone statements (whenGroupId < 0) or when-groups
      struct TopItem
      {
         int order = 0;
         bool isGroup = false;
         int idx = 0;
      };
      std::vector<TopItem> items;
      for (size_t i = 0; i < graph.boxes.size(); ++i)
      {
         const BoxNode& b = graph.boxes[i];
         if (b.stmtOrder >= 0 && b.whenGroupId < 0)
            items.push_back({ b.stmtOrder, false, static_cast<int>(i) });
      }
      for (size_t i = 0; i < graph.groups.size(); ++i)
      {
         const BoxGroup& g = graph.groups[i];
         if (g.isWhenGroup && g.stmtOrder >= 0)
            items.push_back({ g.stmtOrder, true, static_cast<int>(i) });
      }
      std::sort(items.begin(), items.end(), [](const TopItem& a, const TopItem& b) {
         return a.order < b.order;
      });

      for (size_t k = 0; k < items.size(); ++k)
      {
         const TopItem& item = items[k];
         if (!item.isGroup)
         {
            const BoxNode& b = graph.boxes[item.idx];
            if (b.specId == "play_note")
            {
               if (const BoxWire* w0 = graph.FindWireTo(b.id, 0))
               {
                  out << "if (" << printer.PrintBoxExpr(w0->srcBoxId, w0->srcPinIdx, 0) << ") {\n";
                  while (k < items.size() && !items[k].isGroup)
                  {
                     const BoxNode& nb = graph.boxes[items[k].idx];
                     const BoxWire* nw = (nb.specId == "play_note") ? graph.FindWireTo(nb.id, 0) : nullptr;
                     if (!nw || nw->srcBoxId != w0->srcBoxId || nw->srcPinIdx != w0->srcPinIdx)
                        break;
                     out << "   note(" << printer.PrintInlet(nb.id, 1, "60", 0) << ", "
                         << printer.PrintInlet(nb.id, 2, "0.8", 0) << ", "
                         << printer.PrintInlet(nb.id, 3, "0.25", 0) << ")\n";
                     ++k;
                  }
                  --k;
                  out << "}\n";
                  continue;
               }
            }
            std::string line = printer.PrintStmt(b);
            if (!line.empty())
               out << line << "\n";
         }
         else
         {
            const BoxGroup& g = graph.groups[item.idx];
            std::string condStr = printer.PrintBoxExpr(g.condBoxId, g.condPinIdx, 0);
            out << "if (" << condStr << ") {\n";
            std::vector<const BoxNode*> groupBoxes;
            for (int bid : g.boxIds)
            {
               if (const BoxNode* gb = graph.FindBox(bid))
                  groupBoxes.push_back(gb);
            }
            std::sort(groupBoxes.begin(), groupBoxes.end(), [](const BoxNode* a, const BoxNode* b) {
               return a->stmtOrder < b->stmtOrder;
            });
            for (const BoxNode* gb : groupBoxes)
            {
               std::string line = printer.PrintStmt(*gb, "   ");
               if (!line.empty())
                  out << line << "\n";
            }
            out << "}\n";
         }
      }

      return out.str();
   }
}
