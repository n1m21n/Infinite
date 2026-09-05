#include "ElementBackend.h"
#include "FieldRandom.h"
#include "FieldState.h"
#include "ReduceOps.h"

#include <cmath>
#include <algorithm>
#include <unordered_set>

namespace Field
{
   namespace
   {
      struct CodeEmitter
      {
         ElementCompiledCode& compiled;
         int nextReg = 0;
         std::unordered_map<std::string, int> localRegs;
         std::unordered_set<std::string> stateNames;
         bool isPrologue = false;

         // Build step 23: bases this block reads with `.at()`, in first-seen
         // order, deduplicated. Copied onto the program so the VM knows the
         // exact - and usually very short - list of lanes to snapshot.
         std::vector<std::pair<std::string, int>> neighbourBases;

         void RecordNeighbourBase(const std::string& name, int lanes)
         {
            for (auto& nb : neighbourBases)
            {
               if (nb.first == name)
               {
                  if (lanes > nb.second) nb.second = lanes;
                  return;
               }
            }
            neighbourBases.push_back({ name, lanes });
         }

         CodeEmitter(ElementCompiledCode& c, bool prologue)
            : compiled(c), isPrologue(prologue) {}

         int AllocReg()
         {
            int r = nextReg++;
            if (nextReg > compiled.numRegisters)
               compiled.numRegisters = nextReg;
            return r;
         }

         // Register-to-register copy. OpSwizzle with its default identity indices IS
         // the move: dst.v[l] = src.v[l]. Needed because a local must keep ONE register
         // for its whole lifetime - see the loop-carried comment in the Assign lowering.
         void EmitMove(int dstReg, int srcReg, int lanes)
         {
            if (dstReg == srcReg) return;
            ElemInstruction mov;
            mov.op = ElemOpcode::OpSwizzle;
            mov.dst = dstReg;
            mov.src1 = srcReg;
            mov.lanes = lanes;
            compiled.code.push_back(mov);
         }

         int AddConstant(double val)
         {
            ConstantVector cv;
            cv.v[0] = val;
            cv.lanes = 1;
            compiled.constants.push_back(cv);
            return (int)compiled.constants.size() - 1;
         }

         int AddConstantVector(const double* v, int lanes)
         {
            ConstantVector cv;
            for (int i = 0; i < lanes && i < 4; ++i)
               cv.v[i] = v[i];
            cv.lanes = lanes;
            compiled.constants.push_back(cv);
            return (int)compiled.constants.size() - 1;
         }

         int EmitExpr(const IRNodePtr& node)
         {
            if (!node) return 0;

            // Build step 23 (OPEN-B): `X.at(k)`. Checked before the kind switch
            // because the base may be either a mesh attribute (IRKind::Variable)
            // or an element state cell (IRKind::StateRead), and in the latter
            // case the ordinary path would return the pre-loaded local register
            // holding THIS element's value - which is exactly the wrong answer.
            if (node->isNeighbourRead && !node->children.empty())
            {
               int idxReg = EmitExpr(node->children[0]);
               int reg = AllocReg();
               ElemInstruction inst;
               inst.op = ElemOpcode::OpLoadNeighbour;
               inst.dst = reg;
               inst.src1 = idxReg;
               inst.stringData = node->varName;
               inst.lanes = node->type.lanes;
               compiled.code.push_back(inst);
               RecordNeighbourBase(node->varName, node->type.lanes);
               return reg;
            }

            switch (node->kind)
            {
               case IRKind::Literal:
               {
                  int reg = AllocReg();
                  int constIdx = AddConstantVector(node->vecValues, node->type.lanes);
                  ElemInstruction inst;
                  inst.op = ElemOpcode::OpLoadConst;
                  inst.dst = reg;
                  inst.src1 = constIdx;
                  inst.lanes = node->type.lanes;
                  compiled.code.push_back(inst);
                  return reg;
               }

               case IRKind::StateRead:
               {
                  auto it = localRegs.find(node->varName);
                  if (it != localRegs.end()) return it->second;
                  int reg = AllocReg();
                  ElemInstruction inst;
                  inst.op = isPrologue ? ElemOpcode::OpLoadVar : ElemOpcode::OpLoadState;
                  inst.dst = reg;
                  inst.stringData = node->varName;
                  inst.lanes = node->type.lanes;
                  compiled.code.push_back(inst);
                  localRegs[node->varName] = reg;
                  return reg;
               }

               case IRKind::Variable:
               {
                  int reg = AllocReg();
                  if (node->varName == "P")
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadP;
                     inst.dst = reg;
                     inst.lanes = 3;
                     compiled.code.push_back(inst);
                     return reg;
                  }
                  if (node->varName == "N")
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadN;
                     inst.dst = reg;
                     inst.lanes = 3;
                     compiled.code.push_back(inst);
                     return reg;
                  }
                  if (node->varName == "uv")
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadUv;
                     inst.dst = reg;
                     inst.lanes = 2;
                     compiled.code.push_back(inst);
                     return reg;
                  }
                  if (node->varName == "Cd")
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadCd;
                     inst.dst = reg;
                     inst.lanes = 3;
                     compiled.code.push_back(inst);
                     return reg;
                  }
                  if (node->varName == "i")
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadIndex;
                     inst.dst = reg;
                     inst.lanes = 1;
                     compiled.code.push_back(inst);
                     return reg;
                  }
                  if (node->varName == "count")
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadCount;
                     inst.dst = reg;
                     inst.lanes = 1;
                     compiled.code.push_back(inst);
                     return reg;
                  }

                  // Check if it's a local register in this block
                  auto it = localRegs.find(node->varName);
                  if (it != localRegs.end())
                  {
                     return it->second;
                  }

                  // Otherwise it's a frame-domain variable or custom attribute
                  if (isPrologue)
                  {
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadVar;
                     inst.dst = reg;
                     inst.stringData = node->varName;
                     inst.lanes = node->type.lanes;
                     compiled.code.push_back(inst);
                  }
                  else
                  {
                     // In loop: could be custom attrib or frame variable
                     ElemInstruction inst;
                     inst.op = ElemOpcode::OpLoadAttrib;
                     inst.dst = reg;
                     inst.stringData = node->varName;
                     inst.lanes = node->type.lanes;
                     compiled.code.push_back(inst);
                  }
                  return reg;
               }

               case IRKind::Access:
               {
                  int baseReg = EmitExpr(node->children[0]);
                  int reg = AllocReg();
                  ElemInstruction inst;
                  inst.op = ElemOpcode::OpSwizzle;
                  inst.dst = reg;
                  inst.src1 = baseReg;
                  inst.lanes = node->type.lanes;
                  inst.src1Lanes = node->children[0]->type.lanes;
                  for (int i = 0; i < 4; ++i)
                     inst.swizzleIndices[i] = node->swizzleIndices[i];
                  compiled.code.push_back(inst);
                  return reg;
               }

               case IRKind::Unary:
               {
                  int opndReg = EmitExpr(node->children[0]);
                  int reg = AllocReg();
                  ElemInstruction inst;
                  inst.op = (node->op == "!") ? ElemOpcode::OpNot : ElemOpcode::OpNeg;
                  inst.dst = reg;
                  inst.src1 = opndReg;
                  inst.lanes = node->type.lanes;
                  compiled.code.push_back(inst);
                  return reg;
               }

               case IRKind::Binary:
               {
                  int lhsReg = EmitExpr(node->children[0]);
                  int rhsReg = EmitExpr(node->children[1]);
                  int reg = AllocReg();

                  ElemInstruction inst;
                  inst.dst = reg;
                  inst.src1 = lhsReg;
                  inst.src2 = rhsReg;
                  inst.lanes = node->type.lanes;
                  inst.src1Lanes = node->children[0]->type.lanes;
                  inst.src2Lanes = node->children[1]->type.lanes;

                  if (node->op == "+") inst.op = ElemOpcode::OpAdd;
                  else if (node->op == "-") inst.op = ElemOpcode::OpSub;
                  else if (node->op == "*") inst.op = ElemOpcode::OpMul;
                  else if (node->op == "/") inst.op = ElemOpcode::OpDiv;
                  else if (node->op == "%") inst.op = ElemOpcode::OpMod;
                  else if (node->op == "^") inst.op = ElemOpcode::OpPow;
                  else if (node->op == "<") inst.op = ElemOpcode::OpLess;
                  else if (node->op == "<=") inst.op = ElemOpcode::OpLessEqual;
                  else if (node->op == ">") inst.op = ElemOpcode::OpGreater;
                  else if (node->op == ">=") inst.op = ElemOpcode::OpGreaterEqual;
                  else if (node->op == "==") inst.op = ElemOpcode::OpEqual;
                  else if (node->op == "!=") inst.op = ElemOpcode::OpNotEqual;
                  else if (node->op == "&&") inst.op = ElemOpcode::OpLogicalAnd;
                  else if (node->op == "||") inst.op = ElemOpcode::OpLogicalOr;

                  compiled.code.push_back(inst);
                  return reg;
               }

               case IRKind::Call:
               {
                  int reg = AllocReg();
                  ElemInstruction inst;
                  inst.dst = reg;
                  inst.lanes = node->type.lanes;

                  if (node->transferKind == TransferKind::Reduce)
                  {
                     std::string reduceOp = node->callee.substr(7); // "sum", "rms", "min", "max", "mean"
                     std::string targetVar;
                     if (!node->children.empty())
                     {
                        const auto& child = node->children[0];
                        if (child->kind == IRKind::Variable || child->kind == IRKind::StateRead)
                           targetVar = child->varName;
                        else if (child->kind == IRKind::Access && !child->children.empty())
                           targetVar = child->children[0]->varName;
                     }

                     inst.op = ElemOpcode::OpReduceElementAttrib;
                     inst.stringData = targetVar;
                     inst.stringData2 = reduceOp;
                     compiled.code.push_back(inst);
                     return reg;
                  }
                  else if (node->transferKind == TransferKind::Resample || node->transferKind == TransferKind::Downsample)
                  {
                     if (!node->children.empty())
                     {
                        return EmitExpr(node->children[0]);
                     }
                     return reg;
                  }

                  if (node->callee == "vec2" || node->callee == "vec3" || node->callee == "vec4")
                  {
                     inst.op = ElemOpcode::OpVecCtor;
                     for (const auto& c : node->children)
                     {
                        int aReg = EmitExpr(c);
                        inst.argRegs.push_back(aReg);
                        inst.argLanes.push_back(c->type.lanes);
                     }
                  }
                  else
                  {
                     inst.op = ElemOpcode::OpCallBuiltin;
                     inst.stringData = node->callee;
                     for (const auto& c : node->children)
                     {
                        int aReg = EmitExpr(c);
                        inst.argRegs.push_back(aReg);
                        inst.argLanes.push_back(c->type.lanes);
                     }
                  }

                  compiled.code.push_back(inst);
                  return reg;
               }
            }
            return 0;
         }

         void EmitStmt(const IRStmtPtr& stmt)
         {
            if (!stmt) return;

            switch (stmt->kind)
            {
               case IRStmtKind::DeclAttrib:
               {
                  // Handled in store setup
                  break;
               }

               case IRStmtKind::Assign:
               {
                  int rhsReg = EmitExpr(stmt->rvalueExpr);

                  if (stmt->assignTarget == "P")
                  {
                     ElemInstruction inst;
                     if (!stmt->assignField.empty())
                     {
                        inst.op = ElemOpcode::OpStorePComp;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.swizzleCount = stmt->swizzleCount;
                        for (int i = 0; i < 4; ++i)
                           inst.swizzleIndices[i] = stmt->swizzleIndices[i];
                     }
                     else
                     {
                        inst.op = ElemOpcode::OpStoreP;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.lanes = 3;
                        inst.src1Lanes = stmt->rvalueExpr->type.lanes;
                     }
                     compiled.code.push_back(inst);
                  }
                  else if (stmt->assignTarget == "N")
                  {
                     ElemInstruction inst;
                     if (!stmt->assignField.empty())
                     {
                        inst.op = ElemOpcode::OpStoreNComp;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.swizzleCount = stmt->swizzleCount;
                        for (int i = 0; i < 4; ++i)
                           inst.swizzleIndices[i] = stmt->swizzleIndices[i];
                     }
                     else
                     {
                        inst.op = ElemOpcode::OpStoreN;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.lanes = 3;
                        inst.src1Lanes = stmt->rvalueExpr->type.lanes;
                     }
                     compiled.code.push_back(inst);
                  }
                  else if (stmt->assignTarget == "uv")
                  {
                     ElemInstruction inst;
                     if (!stmt->assignField.empty())
                     {
                        inst.op = ElemOpcode::OpStoreUvComp;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.swizzleCount = stmt->swizzleCount;
                        for (int i = 0; i < 4; ++i)
                           inst.swizzleIndices[i] = stmt->swizzleIndices[i];
                     }
                     else
                     {
                        inst.op = ElemOpcode::OpStoreUv;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.lanes = 2;
                        inst.src1Lanes = stmt->rvalueExpr->type.lanes;
                     }
                     compiled.code.push_back(inst);
                  }
                  else if (stmt->assignTarget == "Cd")
                  {
                     ElemInstruction inst;
                     if (!stmt->assignField.empty())
                     {
                        inst.op = ElemOpcode::OpStoreCdComp;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.swizzleCount = stmt->swizzleCount;
                        for (int i = 0; i < 4; ++i)
                           inst.swizzleIndices[i] = stmt->swizzleIndices[i];
                     }
                     else
                     {
                        inst.op = ElemOpcode::OpStoreCd;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignOp;
                        inst.lanes = 3;
                        inst.src1Lanes = stmt->rvalueExpr->type.lanes;
                     }
                     compiled.code.push_back(inst);
                  }
                  else
                  {
                     auto it = localRegs.find(stmt->assignTarget);
                     if (it != localRegs.end())
                     {
                        int oldReg = it->second;
                        if (stmt->assignField.empty())
                        {
                           // A local keeps ONE register for its whole lifetime and every
                           // assignment writes into it. Re-pointing the name at a fresh
                           // register made a loop-carried variable invisible to its own
                           // condition, which is emitted BEFORE the body: the condition
                           // kept reading the pre-loop register, so
                           // `for (j = 0; j < 3; j += 1)` never terminated - a hang, not
                           // a wrong answer.
                           int lanes = stmt->rvalueExpr ? stmt->rvalueExpr->type.lanes : 1;
                           if (stmt->assignOp == "=")
                           {
                              EmitMove(oldReg, rhsReg, lanes);
                           }
                           else
                           {
                              ElemInstruction bin;
                              if (stmt->assignOp == "+=") bin.op = ElemOpcode::OpAdd;
                              else if (stmt->assignOp == "-=") bin.op = ElemOpcode::OpSub;
                              else if (stmt->assignOp == "*=") bin.op = ElemOpcode::OpMul;
                              else bin.op = ElemOpcode::OpDiv;
                              bin.dst = oldReg;
                              bin.src1 = oldReg;
                              bin.src2 = rhsReg;
                              bin.lanes = lanes;
                              bin.src1Lanes = bin.lanes;
                              bin.src2Lanes = bin.lanes;
                              compiled.code.push_back(bin);
                           }

                           // Re-publish the frame var on EVERY assignment, not only the
                           // first. The element loop reads a hoisted local through
                           // OpStoreFrameVar's map, so a value written inside an `if` or
                           // `for` body - or by any later top-level re-assignment - was
                           // updated in the register and never seen downstream:
                           // `k = 0` / `if (t > 1) { k = 5 }` / `P.y += k` added 0.
                           // State cells are excluded: their write-back is the separate
                           // StateWrite statement that carries the unit delay.
                           if (isPrologue && stateNames.find(stmt->assignTarget) == stateNames.end())
                           {
                              ElemInstruction pub;
                              pub.op = ElemOpcode::OpStoreFrameVar;
                              pub.src1 = oldReg;
                              pub.stringData = stmt->assignTarget;
                              pub.lanes = lanes;
                              compiled.code.push_back(pub);
                           }
                        }
                        else
                        {
                           int rhsLanes = stmt->rvalueExpr ? stmt->rvalueExpr->type.lanes : 1;
                           int compReg = rhsReg;
                           if (stmt->assignOp != "=")
                           {
                              int oldCompReg = AllocReg();
                              ElemInstruction sw;
                              sw.op = ElemOpcode::OpSwizzle;
                              sw.dst = oldCompReg;
                              sw.src1 = oldReg;
                              sw.lanes = stmt->swizzleCount;
                              for (int i = 0; i < 4; ++i) sw.swizzleIndices[i] = stmt->swizzleIndices[i];
                              compiled.code.push_back(sw);

                              compReg = AllocReg();
                              ElemInstruction bin;
                              if (stmt->assignOp == "+=") bin.op = ElemOpcode::OpAdd;
                              else if (stmt->assignOp == "-=") bin.op = ElemOpcode::OpSub;
                              else if (stmt->assignOp == "*=") bin.op = ElemOpcode::OpMul;
                              else bin.op = ElemOpcode::OpDiv;
                              bin.dst = compReg;
                              bin.src1 = oldCompReg;
                              bin.src2 = rhsReg;
                              bin.lanes = stmt->swizzleCount;
                              bin.src1Lanes = stmt->swizzleCount;
                              bin.src2Lanes = rhsLanes;
                              compiled.code.push_back(bin);
                           }

                           int totalLanes = 3;
                           std::vector<int> argRegs;
                           for (int l = 0; l < totalLanes; ++l)
                           {
                              int swizzleMatch = -1;
                              for (int k = 0; k < stmt->swizzleCount; ++k)
                              {
                                 if (stmt->swizzleIndices[k] == l)
                                 {
                                    swizzleMatch = k;
                                    break;
                                 }
                              }
                              if (swizzleMatch >= 0)
                              {
                                 if (stmt->swizzleCount == 1)
                                 {
                                    argRegs.push_back(compReg);
                                 }
                                 else
                                 {
                                    int cReg = AllocReg();
                                    ElemInstruction sw;
                                    sw.op = ElemOpcode::OpSwizzle;
                                    sw.dst = cReg;
                                    sw.src1 = compReg;
                                    sw.lanes = 1;
                                    sw.swizzleIndices[0] = (uint8_t)swizzleMatch;
                                    compiled.code.push_back(sw);
                                    argRegs.push_back(cReg);
                                 }
                              }
                              else
                              {
                                 int cReg = AllocReg();
                                 ElemInstruction sw;
                                 sw.op = ElemOpcode::OpSwizzle;
                                 sw.dst = cReg;
                                 sw.src1 = oldReg;
                                 sw.lanes = 1;
                                 sw.swizzleIndices[0] = (uint8_t)l;
                                 compiled.code.push_back(sw);
                                 argRegs.push_back(cReg);
                              }
                           }

                           int newVecReg = AllocReg();
                           ElemInstruction vCtor;
                           vCtor.op = ElemOpcode::OpVecCtor;
                           vCtor.dst = newVecReg;
                           vCtor.lanes = totalLanes;
                           vCtor.argRegs = argRegs;
                           vCtor.argLanes.assign(argRegs.size(), 1);
                           compiled.code.push_back(vCtor);
                           localRegs[stmt->assignTarget] = newVecReg;
                        }
                     }
                     else if (isPrologue)
                     {
                        ElemInstruction inst;
                        inst.op = ElemOpcode::OpStoreFrameVar;
                        inst.src1 = rhsReg;
                        inst.stringData = stmt->assignTarget;
                        inst.lanes = stmt->rvalueExpr ? stmt->rvalueExpr->type.lanes : 1;
                        compiled.code.push_back(inst);
                        localRegs[stmt->assignTarget] = rhsReg;
                     }
                     else
                     {
                        ElemInstruction inst;
                        if (!stmt->assignField.empty())
                        {
                           inst.op = ElemOpcode::OpStoreAttribComp;
                           inst.src1 = rhsReg;
                           inst.stringData = stmt->assignTarget + ":" + stmt->assignOp;
                           inst.swizzleCount = stmt->swizzleCount;
                           for (int i = 0; i < 4; ++i)
                              inst.swizzleIndices[i] = stmt->swizzleIndices[i];
                        }
                        else
                        {
                           inst.op = ElemOpcode::OpStoreAttrib;
                           inst.src1 = rhsReg;
                           inst.stringData = stmt->assignTarget + ":" + stmt->assignOp;
                           inst.lanes = stmt->rvalueExpr ? stmt->rvalueExpr->type.lanes : 1;
                        }
                        compiled.code.push_back(inst);
                        localRegs[stmt->assignTarget] = rhsReg;
                     }
                  }
                  break;
               }

               case IRStmtKind::DeclState:
                  break;

               case IRStmtKind::StateWrite:
               {
                  int srcReg = -1;
                  auto it = localRegs.find(stmt->assignTarget);
                  if (it != localRegs.end())
                  {
                     srcReg = it->second;
                  }
                  else
                  {
                     srcReg = EmitExpr(stmt->rvalueExpr);
                  }
                  ElemInstruction inst;
                  inst.op = isPrologue ? ElemOpcode::OpStoreFrameVar : ElemOpcode::OpStoreState;
                  inst.src1 = srcReg;
                  inst.stringData = stmt->assignTarget;
                  inst.stringData2 = "=";
                  inst.lanes = stmt->stateLanes;
                  inst.src1Lanes = stmt->stateLanes;
                  compiled.code.push_back(inst);
                  break;
               }

               case IRStmtKind::If:
               {
                  int condReg = EmitExpr(stmt->ifCond);

                  int jumpIfFalseIdx = (int)compiled.code.size();
                  ElemInstruction jmpFalse;
                  jmpFalse.op = ElemOpcode::OpJumpIfFalse;
                  jmpFalse.src1 = condReg;
                  compiled.code.push_back(jmpFalse);

                  for (const auto& s : stmt->thenStmts)
                     EmitStmt(s);

                  if (!stmt->elseStmts.empty())
                  {
                     int jumpEndIdx = (int)compiled.code.size();
                     ElemInstruction jmpEnd;
                     jmpEnd.op = ElemOpcode::OpJump;
                     compiled.code.push_back(jmpEnd);

                     compiled.code[jumpIfFalseIdx].jumpTarget = (int)compiled.code.size();

                     for (const auto& s : stmt->elseStmts)
                        EmitStmt(s);

                     compiled.code[jumpEndIdx].jumpTarget = (int)compiled.code.size();
                  }
                  else
                  {
                     compiled.code[jumpIfFalseIdx].jumpTarget = (int)compiled.code.size();
                  }
                  break;
               }

               case IRStmtKind::For:
               {
                  if (stmt->forInit) EmitStmt(stmt->forInit);

                  int loopHead = (int)compiled.code.size();
                  int condReg = EmitExpr(stmt->forCond);

                  int jumpOutIdx = (int)compiled.code.size();
                  ElemInstruction jmpOut;
                  jmpOut.op = ElemOpcode::OpJumpIfFalse;
                  jmpOut.src1 = condReg;
                  compiled.code.push_back(jmpOut);

                  for (const auto& s : stmt->forBody)
                     EmitStmt(s);

                  if (stmt->forStep) EmitStmt(stmt->forStep);

                  ElemInstruction jmpHead;
                  jmpHead.op = ElemOpcode::OpJump;
                  jmpHead.jumpTarget = loopHead;
                  compiled.code.push_back(jmpHead);

                  compiled.code[jumpOutIdx].jumpTarget = (int)compiled.code.size();
                  break;
               }

               case IRStmtKind::Expr:
               {
                  EmitExpr(stmt->expr);
                  break;
               }
            }
         }
      };
   }

   bool EmitElementBytecode(const ElementIRProgram& ir, ElementProgram& outProgram, FieldError& outError)
   {
      outError.Clear();
      outProgram = ElementProgram{};
      outProgram.writeMask = ir.writeMask;
      outProgram.declaredAttribs = ir.declaredAttribs;
      outProgram.declaredParams = ir.declaredParams;
      outProgram.declaredStates = ir.declaredStates;
      outProgram.isTimeDependent = ir.isTimeDependent;

      // 1. Emit Prologue bytecode (Frame domain, runs once per cook)
      {
         CodeEmitter pe(outProgram.prologue, true);
         for (const auto& ds : ir.declaredStates)
            pe.stateNames.insert(ds.name);
         for (const auto& ds : ir.declaredStates)
         {
            if (ds.domain == Domain::Frame || ds.domain == Domain::Graph)
            {
               int reg = pe.AllocReg();
               ElemInstruction inst;
               inst.op = ElemOpcode::OpLoadVar;
               inst.dst = reg;
               inst.stringData = ds.name;
               inst.lanes = ds.lanes;
               pe.compiled.code.push_back(inst);
               pe.localRegs[ds.name] = reg;
            }
         }
         for (const auto& s : ir.prologue)
         {
            pe.EmitStmt(s);
         }
         ElemInstruction ret;
         ret.op = ElemOpcode::OpReturn;
         outProgram.prologue.code.push_back(ret);
      }

      // 2. Emit Element Loop bytecode (Element domain, runs per vertex)
      {
         CodeEmitter le(outProgram.loop, false);
         for (const auto& ds : ir.declaredStates)
            le.stateNames.insert(ds.name);
         for (const auto& ds : ir.declaredStates)
         {
            if (ds.domain == Domain::Element)
            {
               int reg = le.AllocReg();
               ElemInstruction inst;
               inst.op = ElemOpcode::OpLoadState;
               inst.dst = reg;
               inst.stringData = ds.name;
               inst.lanes = ds.lanes;
               le.compiled.code.push_back(inst);
               le.localRegs[ds.name] = reg;
            }
         }
         for (const auto& s : ir.elementLoop)
         {
            le.EmitStmt(s);
         }
         outProgram.neighbourBases = le.neighbourBases;
         ElemInstruction ret;
         ret.op = ElemOpcode::OpReturn;
         outProgram.loop.code.push_back(ret);
      }

      return true;
   }

   namespace
   {
      // ONE implementation of the element bytecode's builtins, shared by the prologue
      // interpreter and the per-element loop. They used to be two copies and they had
      // already drifted: the prologue knew only sin/cos/abs/sqrt/rand and silently
      // produced 0.0 for everything else. That was invisible only because a broken
      // pre-scan meant nothing was ever hoisted into the prologue. Every name
      // ValidateFunction (FieldIR.cpp) accepts must be handled here.
      void EvalElementBuiltin(const ElemInstruction& inst,
                              std::vector<VectorResult>& regs,
                              const ExecutionEnv& env)
      {
         VectorResult& dst = regs[inst.dst];
         dst.lanes = inst.lanes;

         const std::string& fn = inst.stringData;
         const size_t nArgs = inst.argRegs.size();

         auto lane = [&](size_t a, int l) -> double {
            const VectorResult& r = regs[inst.argRegs[a]];
            return (a < inst.argLanes.size() && inst.argLanes[a] == 1) ? r.v[0] : r.v[l];
         };
         auto scalar = [&](size_t a) -> double { return regs[inst.argRegs[a]].v[0]; };

         if (fn == "rand" || fn == "noise" || fn == "sh")
         {
            double rMin = 0.0, rMax = 1.0, rSpeed = 1.0, rSeed = 0.0;
            if (nArgs == 1)
            {
               rSpeed = scalar(0);
            }
            else if (nArgs >= 2)
            {
               rMin = scalar(0);
               rMax = scalar(1);
               if (nArgs >= 3) rSpeed = scalar(2);
               if (nArgs >= 4) rSeed = scalar(3);
            }
            const double v = (fn == "sh") ? Sh(rMin, rMax, rSpeed, rSeed, env.t)
                                          : Rand(rMin, rMax, rSpeed, rSeed, env.t);
            for (int l = 0; l < inst.lanes; ++l) dst.v[l] = v;
            return;
         }

         if (fn == "if")
         {
            const double cond = scalar(0);
            for (int l = 0; l < inst.lanes; ++l)
               dst.v[l] = (cond != 0.0) ? lane(1, l) : lane(2, l);
            return;
         }

         // Cross-lane builtins. Their result is NOT a function of lane l alone, so
         // they cannot live in the lane loop below - and every one of them used to
         // fall straight through that loop's if/else chain and leave 0.0 behind.
         // That is why `length(P)` read zero for every vertex, which quietly made
         // the shipping "Radial Ripple" and "Spherical Bulge" presets uniform
         // instead of radial. The comment above this function already required
         // every name ValidateFunction accepts to be handled here; it wasn't.
         if (fn == "length" || fn == "normalize" || fn == "distance" ||
             fn == "dot" || fn == "cross")
         {
            auto argLanesOf = [&](size_t a) -> int {
               int n = (a < inst.argLanes.size()) ? inst.argLanes[a] : regs[inst.argRegs[a]].lanes;
               return std::max(1, std::min(4, n));
            };
            const VectorResult& a0 = regs[inst.argRegs[0]];

            if (fn == "length" || fn == "normalize")
            {
               const int n = argLanesOf(0);
               double sum = 0.0;
               for (int l = 0; l < n; ++l) sum += a0.v[l] * a0.v[l];
               const double len = std::sqrt(sum);
               if (fn == "length")
               {
                  dst.lanes = 1;
                  dst.v[0] = len;
               }
               else
               {
                  // A zero-length vector normalizes to zero rather than NaN: the
                  // element domain has no per-vertex error channel, and one
                  // degenerate vertex must not poison the whole mesh.
                  dst.lanes = inst.lanes;
                  for (int l = 0; l < inst.lanes; ++l)
                     dst.v[l] = (len > 1e-20 && l < n) ? a0.v[l] / len : 0.0;
               }
               return;
            }

            const VectorResult& a1 = regs[inst.argRegs[1]];
            const int n = std::max(argLanesOf(0), argLanesOf(1));

            if (fn == "distance")
            {
               double sum = 0.0;
               for (int l = 0; l < n; ++l)
               {
                  const double d = a0.v[l] - a1.v[l];
                  sum += d * d;
               }
               dst.lanes = 1;
               dst.v[0] = std::sqrt(sum);
               return;
            }

            if (fn == "dot")
            {
               double sum = 0.0;
               for (int l = 0; l < n; ++l) sum += a0.v[l] * a1.v[l];
               dst.lanes = 1;
               dst.v[0] = sum;
               return;
            }

            dst.lanes = 3;
            dst.v[0] = a0.v[1] * a1.v[2] - a0.v[2] * a1.v[1];
            dst.v[1] = a0.v[2] * a1.v[0] - a0.v[0] * a1.v[2];
            dst.v[2] = a0.v[0] * a1.v[1] - a0.v[1] * a1.v[0];
            return;
         }

         for (int l = 0; l < inst.lanes; ++l)
         {
            double r = 0.0;
            if (fn == "sin") r = std::sin(lane(0, l));
            else if (fn == "cos") r = std::cos(lane(0, l));
            else if (fn == "tan") r = std::tan(lane(0, l));
            else if (fn == "abs") r = std::fabs(lane(0, l));
            else if (fn == "floor") r = std::floor(lane(0, l));
            else if (fn == "ceil") r = std::ceil(lane(0, l));
            // Matches Expression.cpp / FieldVM: round is floor(x + 0.5), not std::round.
            else if (fn == "round") r = std::floor(lane(0, l) + 0.5);
            else if (fn == "sign")
            {
               const double x = lane(0, l);
               r = (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0);
            }
            else if (fn == "exp") r = std::exp(lane(0, l));
            // The element domain clamps rather than erroring: there is no per-element
            // error channel, and a whole cook must not be lost to one bad vertex.
            else if (fn == "sqrt") r = std::sqrt(std::max(0.0, lane(0, l)));
            else if (fn == "log") r = std::log(std::max(1e-15, lane(0, l)));
            else if (fn == "min") r = std::min(lane(0, l), lane(1, l));
            else if (fn == "max") r = std::max(lane(0, l), lane(1, l));
            else if (fn == "pow") r = std::pow(lane(0, l), lane(1, l));
            else if (fn == "mod")
            {
               const double d = lane(1, l);
               r = (d != 0.0) ? std::fmod(lane(0, l), d) : 0.0;
            }
            else if (fn == "step") r = (lane(1, l) < lane(0, l)) ? 0.0 : 1.0;
            else if (fn == "fract") r = lane(0, l) - std::floor(lane(0, l));
            else if (fn == "fmod")
            {
               const double d = lane(1, l);
               r = (d != 0.0) ? std::fmod(lane(0, l), d) : 0.0;
            }
            else if (fn == "atan2") r = std::atan2(lane(0, l), lane(1, l));
            else if (fn == "clamp") r = std::max(lane(1, l), std::min(lane(2, l), lane(0, l)));
            else if (fn == "lerp" || fn == "mix")
            {
               const double a = lane(0, l);
               r = a + lane(2, l) * (lane(1, l) - a);
            }
            else if (fn == "smoothstep")
            {
               const double e0 = lane(0, l);
               const double e1 = lane(1, l);
               const double x = (e1 == e0) ? ((lane(2, l) < e0) ? 0.0 : 1.0)
                                           : std::max(0.0, std::min(1.0, (lane(2, l) - e0) / (e1 - e0)));
               r = (e1 == e0) ? x : x * x * (3.0 - 2.0 * x);
            }
            dst.v[l] = r;
         }
      }

      // Resolves a bare name that owns no element storage: the frame-domain reserved
      // words, then a hoisted prologue variable, then a param, then a sibling.
      // Returning 0.0 for `t` inside the element loop is exactly the bug that made
      // the Field Element node's default program flat.
      bool LoadEnvScalar(const std::string& name,
                         const ExecutionEnv& env,
                         size_t elementCount,
                         double& outValue)
      {
         if (name == "t") { outValue = env.t; return true; }
         if (name == "dt") { outValue = env.dt; return true; }
         if (name == "frame") { outValue = env.frame; return true; }
         if (name == "age") { outValue = env.age; return true; }
         if (name == "noteOn") { outValue = env.noteOn; return true; }
         if (name == "notePitch") { outValue = env.notePitch; return true; }
         if (name == "noteVel") { outValue = env.noteVel; return true; }
         if (name == "count") { outValue = (double)elementCount; return true; }
         if (env.params)
         {
            auto it = env.params->find(name);
            if (it != env.params->end()) { outValue = (double)it->second; return true; }
         }
         if (env.siblings)
         {
            auto it = env.siblings->find(name);
            if (it != env.siblings->end()) { outValue = (double)it->second; return true; }
         }
         outValue = 0.0;
         return false;
      }
   }

   // Applies one compound-assignment operator to a mesh lane. Every store opcode goes
   // through this: the hand-written chains used to disagree about which operators they
   // understood (`N /= x` and `Cd -= x` silently became plain assignment), which is the
   // same silent-wrong-answer bug class as a missing opcode.
   static inline void ApplyCompound(float& slot, const std::string& op, double val)
   {
      if (op == "+=") slot += (float)val;
      else if (op == "-=") slot -= (float)val;
      else if (op == "*=") slot *= (float)val;
      else if (op == "/=") { if (val != 0.0) slot /= (float)val; }
      else slot = (float)val;
   }

   bool ElementVM::ExecuteBlock(const ElementCompiledCode& block,
                                std::vector<VectorResult>& regs,
                                const ExecutionEnv& env,
                                const ElementExecContext& ctx,
                                bool& outRanAnInstruction,
                                std::string& outError)
   {
      const auto& code = block.code;
      const auto& consts = block.constants;
      const size_t ei = ctx.index;

      // An element-only opcode outside the element loop means the rate partition put an
      // element statement in the frame prologue - a compiler bug, not a runtime state.
      // It is reported instead of skipped: falling through `default: break` and leaving
      // the destination register at 0 is exactly how the prologue came to be missing
      // every comparison, logical, unary and branch opcode without anyone noticing.
      auto requireElement = [&](const char* opName) -> bool
      {
         if (ctx.inElementLoop) return true;
         if (outError.empty())
            outError = std::string("internal: element opcode '") + opName + "' reached the frame prologue";
         return false;
      };

      // One name-resolution order for both banks: state cell, then element attrib, then
      // a value hoisted into the prologue, then the frame-domain environment.
      auto loadByName = [&](const ElemInstruction& inst)
      {
         VectorResult& dst = regs[inst.dst];

         if (env.state && env.state->HasCell(inst.stringData))
         {
            const auto* cell = env.state->FindCell(inst.stringData);
            if (cell)
            {
               dst.lanes = cell->lanes;
               for (int l = 0; l < cell->lanes; ++l)
               {
                  if (cell->domain == Domain::Element && ctx.inElementLoop)
                  {
                     auto* laneVec = env.state->GetElementLane((size_t)(cell->slotOffset + l));
                     dst.v[l] = (laneVec && ei < laneVec->size()) ? (*laneVec)[ei] : 0.0;
                  }
                  else
                  {
                     const float* fVal = env.state->GetFrameValue((size_t)(cell->slotOffset + l));
                     dst.v[l] = fVal ? *fVal : 0.0;
                  }
               }
               return;
            }
         }

         if (ctx.inElementLoop && ctx.store && ctx.store->HasAttrib(inst.stringData))
         {
            int lanes = inst.lanes;
            dst.lanes = lanes;
            for (int l = 0; l < lanes; ++l)
            {
               auto laneVec = ctx.store->GetAttribLane(inst.stringData, l);
               dst.v[l] = laneVec ? (*laneVec)[ei] : 0.0;
            }
            return;
         }

         auto it = mFrameVars.find(inst.stringData);
         if (it != mFrameVars.end())
         {
            dst = it->second;
            return;
         }

         // A name that resolves nowhere is rejected at IR lowering as undeclared; the
         // 0.0 here is only the residual for a forward reference, which by construction
         // has no value yet.
         double val = 0.0;
         LoadEnvScalar(inst.stringData, env, ctx.count, val);
         dst.lanes = 1;
         dst.v[0] = val;
      };

      size_t pc = 0;
      while (pc < code.size())
      {
         const ElemInstruction& inst = code[pc];
         if (inst.op == ElemOpcode::OpReturn)
            break;
         outRanAnInstruction = true;

         switch (inst.op)
         {
            case ElemOpcode::OpLoadConst:
            {
               const ConstantVector& cv = consts[inst.src1];
               regs[inst.dst].lanes = cv.lanes;
               for (int i = 0; i < cv.lanes; ++i)
                  regs[inst.dst].v[i] = cv.v[i];
               break;
            }

            case ElemOpcode::OpLoadVar:
            case ElemOpcode::OpLoadAttrib:
            case ElemOpcode::OpLoadState:
            case ElemOpcode::OpLoadFrameVar:
               loadByName(inst);
               break;

            case ElemOpcode::OpStoreFrameVar:
            {
               mFrameVars[inst.stringData] = regs[inst.src1];
               if (env.state && env.state->HasCell(inst.stringData))
               {
                  const auto* cell = env.state->FindCell(inst.stringData);
                  if (cell)
                  {
                     const auto& src = regs[inst.src1];
                     for (int l = 0; l < cell->lanes; ++l)
                     {
                        float* fVal = env.state->GetFrameValue((size_t)(cell->slotOffset + l));
                        if (fVal)
                        {
                           double val = (cell->lanes == 1 || src.lanes == 1) ? src.v[0] : src.v[l];
                           *fVal = (float)val;
                        }
                     }
                  }
               }
               break;
            }

            case ElemOpcode::OpLoadP:
            case ElemOpcode::OpLoadN:
            case ElemOpcode::OpLoadUv:
            case ElemOpcode::OpLoadCd:
            {
               if (!requireElement("load builtin attribute")) return false;
               VectorResult& dst = regs[inst.dst];
               if (inst.op == ElemOpcode::OpLoadP)
               {
                  dst.lanes = 3;
                  dst.v[0] = ctx.px[ei]; dst.v[1] = ctx.py[ei]; dst.v[2] = ctx.pz[ei];
               }
               else if (inst.op == ElemOpcode::OpLoadN)
               {
                  dst.lanes = 3;
                  dst.v[0] = ctx.nx[ei]; dst.v[1] = ctx.ny[ei]; dst.v[2] = ctx.nz[ei];
               }
               else if (inst.op == ElemOpcode::OpLoadUv)
               {
                  dst.lanes = 2;
                  dst.v[0] = ctx.u[ei]; dst.v[1] = ctx.v[ei];
               }
               else
               {
                  dst.lanes = 3;
                  dst.v[0] = ctx.cr[ei]; dst.v[1] = ctx.cg[ei]; dst.v[2] = ctx.cb[ei];
               }
               break;
            }

            case ElemOpcode::OpStoreP:
            case ElemOpcode::OpStoreN:
            case ElemOpcode::OpStoreUv:
            case ElemOpcode::OpStoreCd:
            {
               if (!requireElement("store builtin attribute")) return false;
               float* comps[3] = { nullptr, nullptr, nullptr };
               int n = 3;
               if (inst.op == ElemOpcode::OpStoreP) { comps[0] = &ctx.px[ei]; comps[1] = &ctx.py[ei]; comps[2] = &ctx.pz[ei]; }
               else if (inst.op == ElemOpcode::OpStoreN) { comps[0] = &ctx.nx[ei]; comps[1] = &ctx.ny[ei]; comps[2] = &ctx.nz[ei]; }
               else if (inst.op == ElemOpcode::OpStoreUv) { comps[0] = &ctx.u[ei]; comps[1] = &ctx.v[ei]; n = 2; }
               else { comps[0] = &ctx.cr[ei]; comps[1] = &ctx.cg[ei]; comps[2] = &ctx.cb[ei]; }

               const auto& src = regs[inst.src1];
               for (int l = 0; l < n; ++l)
                  ApplyCompound(*comps[l], inst.stringData, (inst.src1Lanes == 1) ? src.v[0] : src.v[l]);
               break;
            }

            case ElemOpcode::OpStorePComp:
            case ElemOpcode::OpStoreNComp:
            case ElemOpcode::OpStoreUvComp:
            case ElemOpcode::OpStoreCdComp:
            {
               if (!requireElement("store builtin attribute component")) return false;
               float* comps[3] = { nullptr, nullptr, nullptr };
               if (inst.op == ElemOpcode::OpStorePComp) { comps[0] = &ctx.px[ei]; comps[1] = &ctx.py[ei]; comps[2] = &ctx.pz[ei]; }
               else if (inst.op == ElemOpcode::OpStoreNComp) { comps[0] = &ctx.nx[ei]; comps[1] = &ctx.ny[ei]; comps[2] = &ctx.nz[ei]; }
               else if (inst.op == ElemOpcode::OpStoreUvComp) { comps[0] = &ctx.u[ei]; comps[1] = &ctx.v[ei]; }
               else { comps[0] = &ctx.cr[ei]; comps[1] = &ctx.cg[ei]; comps[2] = &ctx.cb[ei]; }

               const auto& src = regs[inst.src1];
               for (int k = 0; k < inst.swizzleCount; ++k)
               {
                  int idx = inst.swizzleIndices[k];
                  if (idx < 0 || idx > 2 || comps[idx] == nullptr) continue;
                  ApplyCompound(*comps[idx], inst.stringData, (inst.swizzleCount == 1) ? src.v[0] : src.v[k]);
               }
               break;
            }

            case ElemOpcode::OpLoadNeighbour:
            {
               if (!requireElement("neighbour read")) return false;
               VectorResult& dst = regs[inst.dst];
               dst.lanes = inst.lanes;

               // Clamped, per the OPEN-B decision: element 0 asking for i-1 sees
               // itself rather than wrapping to the far end of the mesh, so an
               // open chain behaves like an open chain instead of a torus.
               long long k = (long long)std::llround(regs[inst.src1].v[0]);
               if (k < 0) k = 0;
               if (ctx.count > 0 && (size_t)k >= ctx.count) k = (long long)ctx.count - 1;

               auto snap = mNeighbourSnapshot.find(inst.stringData);
               for (int l = 0; l < inst.lanes; ++l)
               {
                  double val = 0.0;
                  if (snap != mNeighbourSnapshot.end() && l < (int)snap->second.size())
                  {
                     const std::vector<float>& lane = snap->second[l];
                     if ((size_t)k < lane.size()) val = (double)lane[(size_t)k];
                  }
                  dst.v[l] = val;
               }
               break;
            }

            case ElemOpcode::OpLoadIndex:
            {
               if (!requireElement("index")) return false;
               regs[inst.dst].lanes = 1;
               regs[inst.dst].v[0] = (double)ei;
               break;
            }

            case ElemOpcode::OpLoadCount:
            {
               regs[inst.dst].lanes = 1;
               regs[inst.dst].v[0] = (double)ctx.count;
               break;
            }

            case ElemOpcode::OpStoreAttrib:
            case ElemOpcode::OpStoreAttribComp:
            case ElemOpcode::OpStoreState:
            case ElemOpcode::OpStoreStateComp:
            {
               const bool isComp = (inst.op == ElemOpcode::OpStoreAttribComp ||
                                    inst.op == ElemOpcode::OpStoreStateComp);
               const bool isState = (inst.op == ElemOpcode::OpStoreState ||
                                     inst.op == ElemOpcode::OpStoreStateComp);

               std::string name = inst.stringData;
               std::string op = "=";
               if (isState)
               {
                  op = inst.stringData2.empty() ? "=" : inst.stringData2;
               }
               else
               {
                  size_t colon = inst.stringData.find(':');
                  if (colon != std::string::npos)
                  {
                     name = inst.stringData.substr(0, colon);
                     op = inst.stringData.substr(colon + 1);
                  }
               }

               const auto& src = regs[inst.src1];

               if (env.state && env.state->HasCell(name))
               {
                  const auto* cell = env.state->FindCell(name);
                  if (cell)
                  {
                     int n = isComp ? inst.swizzleCount : cell->lanes;
                     for (int k = 0; k < n; ++k)
                     {
                        int slot = isComp ? inst.swizzleIndices[k] : k;
                        double val;
                        if (isComp) val = (inst.swizzleCount == 1) ? src.v[0] : src.v[k];
                        else val = (cell->lanes == 1 || inst.src1Lanes == 1) ? src.v[0] : src.v[k];

                        if (cell->domain == Domain::Element)
                        {
                           if (!requireElement("store to element state cell")) return false;
                           auto* laneVec = env.state->GetElementLane((size_t)(cell->slotOffset + slot));
                           if (laneVec && ei < laneVec->size())
                              ApplyCompound((*laneVec)[ei], op, val);
                        }
                        else
                        {
                           float* fVal = env.state->GetFrameValue((size_t)(cell->slotOffset + slot));
                           if (fVal) ApplyCompound(*fVal, op, val);
                        }
                     }
                  }
                  break;
               }

               if (ctx.store && ctx.store->HasAttrib(name))
               {
                  if (!requireElement("store to element attrib")) return false;
                  int n = isComp ? inst.swizzleCount : inst.lanes;
                  for (int k = 0; k < n; ++k)
                  {
                     int slot = isComp ? inst.swizzleIndices[k] : k;
                     auto laneVec = ctx.store->GetAttribLane(name, slot);
                     if (!laneVec) continue;
                     double val;
                     if (isComp) val = (inst.swizzleCount == 1) ? src.v[0] : src.v[k];
                     else val = (src.lanes == 1) ? src.v[0] : src.v[k];
                     ApplyCompound((*laneVec)[ei], op, val);
                  }
               }
               break;
            }

            case ElemOpcode::OpReduceElementAttrib:
            {
               outRanAnInstruction = true;
               const std::string& name = inst.stringData;
               const std::string& opName = inst.stringData2;
               ReduceOpKind opKind = ParseReduceOpKind(opName);
               auto& dst = regs[inst.dst];
               dst.lanes = inst.lanes;

               if (!ctx.store || ctx.count == 0)
               {
                  for (int l = 0; l < inst.lanes; ++l) dst.v[l] = 0.0;
                  break;
               }

               const float* lanePtrs[4] = { nullptr, nullptr, nullptr, nullptr };
               if (name == "P")
               {
                  if (ctx.store->Px().size() >= ctx.count) lanePtrs[0] = ctx.store->Px().data();
                  if (ctx.store->Py().size() >= ctx.count) lanePtrs[1] = ctx.store->Py().data();
                  if (ctx.store->Pz().size() >= ctx.count) lanePtrs[2] = ctx.store->Pz().data();
               }
               else if (name == "N")
               {
                  if (ctx.store->Nx().size() >= ctx.count) lanePtrs[0] = ctx.store->Nx().data();
                  if (ctx.store->Ny().size() >= ctx.count) lanePtrs[1] = ctx.store->Ny().data();
                  if (ctx.store->Nz().size() >= ctx.count) lanePtrs[2] = ctx.store->Nz().data();
               }
               else if (name == "uv")
               {
                  if (ctx.store->U().size() >= ctx.count) lanePtrs[0] = ctx.store->U().data();
                  if (ctx.store->V().size() >= ctx.count) lanePtrs[1] = ctx.store->V().data();
               }
               else if (name == "Cd")
               {
                  if (ctx.store->Cr().size() >= ctx.count) lanePtrs[0] = ctx.store->Cr().data();
                  if (ctx.store->Cg().size() >= ctx.count) lanePtrs[1] = ctx.store->Cg().data();
                  if (ctx.store->Cb().size() >= ctx.count) lanePtrs[2] = ctx.store->Cb().data();
               }
               else
               {
                  for (int l = 0; l < inst.lanes; ++l)
                  {
                     const auto* lVec = ctx.store->GetAttribLane(name, l);
                     if (lVec && lVec->size() >= ctx.count)
                        lanePtrs[l] = lVec->data();
                  }
               }

               for (int l = 0; l < inst.lanes; ++l)
               {
                  if (lanePtrs[l])
                  {
                     switch (opKind)
                     {
                        case ReduceOpKind::Sum:
                           dst.v[l] = ReduceSum(lanePtrs[l], ctx.count);
                           break;
                        case ReduceOpKind::Mean:
                           dst.v[l] = ReduceMean(lanePtrs[l], ctx.count);
                           break;
                        case ReduceOpKind::Rms:
                           dst.v[l] = ReduceRms(lanePtrs[l], ctx.count);
                           break;
                        case ReduceOpKind::Min:
                           dst.v[l] = ReduceMin(lanePtrs[l], ctx.count);
                           break;
                        case ReduceOpKind::Max:
                           dst.v[l] = ReduceMax(lanePtrs[l], ctx.count);
                           break;
                        default:
                           dst.v[l] = 0.0;
                           break;
                     }
                  }
                  else
                  {
                     dst.v[l] = 0.0;
                  }
               }
               break;
            }

            case ElemOpcode::OpAdd:
            case ElemOpcode::OpSub:
            case ElemOpcode::OpMul:
            case ElemOpcode::OpDiv:
            case ElemOpcode::OpMod:
            case ElemOpcode::OpPow:
            {
               const auto& r1 = regs[inst.src1];
               const auto& r2 = regs[inst.src2];
               auto& dst = regs[inst.dst];
               dst.lanes = inst.lanes;

               for (int l = 0; l < inst.lanes; ++l)
               {
                  double v1 = (inst.src1Lanes == 1) ? r1.v[0] : r1.v[l];
                  double v2 = (inst.src2Lanes == 1) ? r2.v[0] : r2.v[l];
                  double res = 0.0;

                  if (inst.op == ElemOpcode::OpAdd) res = v1 + v2;
                  else if (inst.op == ElemOpcode::OpSub) res = v1 - v2;
                  else if (inst.op == ElemOpcode::OpMul) res = v1 * v2;
                  else if (inst.op == ElemOpcode::OpDiv) res = (v2 != 0.0) ? (v1 / v2) : 0.0;
                  else if (inst.op == ElemOpcode::OpMod) res = (v2 != 0.0) ? std::fmod(v1, v2) : 0.0;
                  else if (inst.op == ElemOpcode::OpPow) res = std::pow(v1, v2);

                  dst.v[l] = res;
               }
               break;
            }

            case ElemOpcode::OpLess:
            case ElemOpcode::OpLessEqual:
            case ElemOpcode::OpGreater:
            case ElemOpcode::OpGreaterEqual:
            case ElemOpcode::OpEqual:
            case ElemOpcode::OpNotEqual:
            {
               double v1 = regs[inst.src1].v[0];
               double v2 = regs[inst.src2].v[0];
               bool res = false;
               if (inst.op == ElemOpcode::OpLess) res = (v1 < v2);
               else if (inst.op == ElemOpcode::OpLessEqual) res = (v1 <= v2);
               else if (inst.op == ElemOpcode::OpGreater) res = (v1 > v2);
               else if (inst.op == ElemOpcode::OpGreaterEqual) res = (v1 >= v2);
               else if (inst.op == ElemOpcode::OpEqual) res = (v1 == v2);
               else if (inst.op == ElemOpcode::OpNotEqual) res = (v1 != v2);

               regs[inst.dst].lanes = 1;
               regs[inst.dst].v[0] = res ? 1.0 : 0.0;
               break;
            }

            case ElemOpcode::OpLogicalAnd:
            case ElemOpcode::OpLogicalOr:
            {
               double v1 = regs[inst.src1].v[0];
               double v2 = regs[inst.src2].v[0];
               bool res = (inst.op == ElemOpcode::OpLogicalAnd) ? (v1 != 0.0 && v2 != 0.0)
                                                                : (v1 != 0.0 || v2 != 0.0);
               regs[inst.dst].lanes = 1;
               regs[inst.dst].v[0] = res ? 1.0 : 0.0;
               break;
            }

            case ElemOpcode::OpNeg:
            {
               const auto& src = regs[inst.src1];
               auto& dst = regs[inst.dst];
               dst.lanes = inst.lanes;
               for (int l = 0; l < inst.lanes; ++l) dst.v[l] = -src.v[l];
               break;
            }

            case ElemOpcode::OpNot:
            {
               double v = regs[inst.src1].v[0];
               regs[inst.dst].lanes = 1;
               regs[inst.dst].v[0] = (v == 0.0) ? 1.0 : 0.0;
               break;
            }

            case ElemOpcode::OpCallBuiltin:
               EvalElementBuiltin(inst, regs, env);
               break;

            case ElemOpcode::OpVecCtor:
            {
               auto& dst = regs[inst.dst];
               dst.lanes = inst.lanes;
               if (inst.argRegs.size() == 1 && inst.argLanes[0] == 1)
               {
                  double s = regs[inst.argRegs[0]].v[0];
                  for (int l = 0; l < inst.lanes; ++l) dst.v[l] = s;
               }
               else
               {
                  int outIdx = 0;
                  for (size_t a = 0; a < inst.argRegs.size(); ++a)
                  {
                     const auto& arg = regs[inst.argRegs[a]];
                     for (int l = 0; l < inst.argLanes[a] && outIdx < inst.lanes; ++l)
                        dst.v[outIdx++] = arg.v[l];
                  }
               }
               break;
            }

            case ElemOpcode::OpSwizzle:
            {
               const auto& src = regs[inst.src1];
               auto& dst = regs[inst.dst];
               dst.lanes = inst.lanes;
               for (int l = 0; l < inst.lanes; ++l)
                  dst.v[l] = src.v[inst.swizzleIndices[l]];
               break;
            }

            case ElemOpcode::OpJumpIfFalse:
            {
               if (regs[inst.src1].v[0] == 0.0)
               {
                  pc = (size_t)inst.jumpTarget;
                  continue;
               }
               break;
            }

            case ElemOpcode::OpJump:
               pc = (size_t)inst.jumpTarget;
               continue;

            case ElemOpcode::OpReturn:
               break;

            // No `default:` on purpose. Every ElemOpcode is named above, so adding one
            // to the enum without teaching this switch is a -Wswitch compile error
            // rather than a register that silently stays 0.
         }
         pc++;
      }

      return true;
   }

   bool ElementVM::Execute(const ElementProgram& prog,
                           ElementStore& store,
                           const ExecutionEnv& env,
                           std::string& outError)
   {
      outError.clear();

      // Allocate / size register banks
      if (mFrameRegisters.size() < (size_t)prog.prologue.numRegisters)
         mFrameRegisters.resize(prog.prologue.numRegisters);
      if (mLoopRegisters.size() < (size_t)prog.loop.numRegisters)
         mLoopRegisters.resize(prog.loop.numRegisters);

      mFrameVars.clear();

      size_t count = store.Count();

      // 1. Run Prologue (Frame domain) ONCE per cook (Field spec 5.5)
      // The counter is bumped only once the prologue has actually executed an
      // instruction. Bumping it unconditionally made "prologue ran exactly 1
      // time" true even for an EMPTY prologue, so the hoisting assertion could not
      // fail - and a test that cannot fail is not a test (field-testing 0.2).
      ElementExecContext frameCtx;
      frameCtx.inElementLoop = false;
      frameCtx.count = count;
      frameCtx.store = &store;

      bool prologueRanAnInstruction = false;
      if (!ExecuteBlock(prog.prologue, mFrameRegisters, env, frameCtx, prologueRanAnInstruction, outError))
         return false;

      if (prologueRanAnInstruction)
         prog.prologueEvalCount++;

      // 2. Run Element Loop per element (0..store.Count()-1)
      if (count == 0) return true;

      // Build step 23 (OPEN-B): snapshot the bases this kernel reads with
      // `.at()`, ONCE, before any element runs. Every neighbour read then sees
      // the cook's input - the incoming mesh for an attribute, the previous
      // cook's value for an element state cell - so the loop is a pure function
      // of its input and element order cannot change the result. Only the
      // named bases are copied: a kernel with no `.at()` copies nothing.
      for (const auto& nb : prog.neighbourBases)
      {
         const std::string& name = nb.first;
         const int lanes = nb.second;

         std::vector<std::vector<float>>& dst = mNeighbourSnapshot[name];
         if ((int)dst.size() < lanes) dst.resize(lanes);

         const std::vector<float>* src[4] = { nullptr, nullptr, nullptr, nullptr };
         if (name == "P") { src[0] = &store.Px(); src[1] = &store.Py(); src[2] = &store.Pz(); }
         else if (name == "N") { src[0] = &store.Nx(); src[1] = &store.Ny(); src[2] = &store.Nz(); }
         else if (name == "uv") { src[0] = &store.U(); src[1] = &store.V(); }
         else if (name == "Cd") { src[0] = &store.Cr(); src[1] = &store.Cg(); src[2] = &store.Cb(); }
         else if (store.HasAttrib(name))
         {
            for (int l = 0; l < lanes && l < 4; ++l)
               src[l] = store.GetAttribLane(name, l);
         }
         else if (env.state && env.state->HasCell(name))
         {
            const StateCell* cell = env.state->FindCell(name);
            if (cell && cell->domain == Domain::Element)
            {
               for (int l = 0; l < lanes && l < cell->lanes && l < 4; ++l)
                  src[l] = env.state->GetElementLane((size_t)(cell->slotOffset + l));
            }
         }

         for (int l = 0; l < lanes && l < 4; ++l)
         {
            if (src[l]) dst[l] = *src[l];
            else dst[l].assign(count, 0.0f);
         }
      }

      ElementExecContext elemCtx;
      elemCtx.inElementLoop = true;
      elemCtx.count = count;
      elemCtx.store = &store;
      elemCtx.px = store.Px().data();
      elemCtx.py = store.Py().data();
      elemCtx.pz = store.Pz().data();
      elemCtx.nx = store.Nx().data();
      elemCtx.ny = store.Ny().data();
      elemCtx.nz = store.Nz().data();
      elemCtx.u = store.U().data();
      elemCtx.v = store.V().data();
      elemCtx.cr = store.Cr().data();
      elemCtx.cg = store.Cg().data();
      elemCtx.cb = store.Cb().data();

      for (size_t elemIdx = 0; elemIdx < count; ++elemIdx)
      {
         prog.elementEvalCount++;
         elemCtx.index = elemIdx;

         bool ranAnInstruction = false;
         if (!ExecuteBlock(prog.loop, mLoopRegisters, env, elemCtx, ranAnInstruction, outError))
            return false;
      }

      return true;
   }
}
