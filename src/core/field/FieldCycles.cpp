#include "FieldCycles.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <unordered_set>

namespace Field
{
   namespace
   {
      void CollectReads(const IRNodePtr& node, std::vector<std::string>& outReads)
      {
         if (!node) return;
         if (node->kind == IRKind::Variable || node->kind == IRKind::StateRead)
         {
            outReads.push_back(node->varName);
         }
         for (const auto& c : node->children)
         {
            CollectReads(c, outReads);
         }
      }

      struct GraphNode
      {
         int id = 0;
         std::string name;
         SourceSpan span;
         bool isStateRead = false;
         bool isStateWrite = false;
      };

      struct GraphEdge
      {
         int to = 0;
         bool isDelay = false;
      };
   }

   IRNodePtr FoldConstants(const IRNodePtr& node)
   {
      if (!node) return nullptr;

      // Fold children first
      for (size_t i = 0; i < node->children.size(); ++i)
      {
         node->children[i] = FoldConstants(node->children[i]);
      }

      if (node->kind == IRKind::Binary)
      {
         auto lhs = node->children[0];
         auto rhs = node->children[1];

         // Multiplication by 0: x * 0 or 0 * x -> 0
         if (node->op == "*")
         {
            if (lhs && lhs->kind == IRKind::Literal && lhs->numberValue == 0.0)
            {
               auto res = std::make_shared<IRNode>(IRKind::Literal, node->span);
               res->type = node->type;
               res->domain = Domain::Graph;
               res->numberValue = 0.0;
               return res;
            }
            if (rhs && rhs->kind == IRKind::Literal && rhs->numberValue == 0.0)
            {
               auto res = std::make_shared<IRNode>(IRKind::Literal, node->span);
               res->type = node->type;
               res->domain = Domain::Graph;
               res->numberValue = 0.0;
               return res;
            }
            if (lhs && lhs->kind == IRKind::Literal && lhs->numberValue == 1.0)
            {
               return rhs;
            }
            if (rhs && rhs->kind == IRKind::Literal && rhs->numberValue == 1.0)
            {
               return lhs;
            }
         }

         // Addition with 0: x + 0 or 0 + x -> x
         if (node->op == "+")
         {
            if (lhs && lhs->kind == IRKind::Literal && lhs->numberValue == 0.0)
            {
               return rhs;
            }
            if (rhs && rhs->kind == IRKind::Literal && rhs->numberValue == 0.0)
            {
               return lhs;
            }
         }

         // Subtraction with 0: x - 0 -> x
         if (node->op == "-")
         {
            if (rhs && rhs->kind == IRKind::Literal && rhs->numberValue == 0.0)
            {
               return lhs;
            }
         }

         // Both literals
         if (lhs && rhs && lhs->kind == IRKind::Literal && rhs->kind == IRKind::Literal)
         {
            double a = lhs->numberValue;
            double b = rhs->numberValue;
            double r = 0.0;
            bool folded = false;

            if (node->op == "+") { r = a + b; folded = true; }
            else if (node->op == "-") { r = a - b; folded = true; }
            else if (node->op == "*") { r = a * b; folded = true; }
            else if (node->op == "/" && b != 0.0) { r = a / b; folded = true; }
            else if (node->op == "==") { r = (a == b) ? 1.0 : 0.0; folded = true; }
            else if (node->op == "!=") { r = (a != b) ? 1.0 : 0.0; folded = true; }
            else if (node->op == "<") { r = (a < b) ? 1.0 : 0.0; folded = true; }
            else if (node->op == "<=") { r = (a <= b) ? 1.0 : 0.0; folded = true; }
            else if (node->op == ">") { r = (a > b) ? 1.0 : 0.0; folded = true; }
            else if (node->op == ">=") { r = (a >= b) ? 1.0 : 0.0; folded = true; }

            if (folded)
            {
               auto res = std::make_shared<IRNode>(IRKind::Literal, node->span);
               res->type = node->type;
               res->domain = Domain::Graph;
               res->numberValue = r;
               res->vecValues[0] = r;
               return res;
            }
         }
      }
      else if (node->kind == IRKind::Unary)
      {
         auto child = node->children[0];
         if (child && child->kind == IRKind::Literal)
         {
            if (node->op == "-")
            {
               auto res = std::make_shared<IRNode>(IRKind::Literal, node->span);
               res->type = node->type;
               res->domain = Domain::Graph;
               res->numberValue = -child->numberValue;
               res->vecValues[0] = -child->numberValue;
               return res;
            }
            if (node->op == "!")
            {
               auto res = std::make_shared<IRNode>(IRKind::Literal, node->span);
               res->type = FieldType(DataType::Bool, 1);
               res->domain = Domain::Graph;
               res->numberValue = (child->numberValue == 0.0) ? 1.0 : 0.0;
               res->vecValues[0] = res->numberValue;
               return res;
            }
         }
      }

      return node;
   }

   void FoldConstantsInStmts(std::vector<IRStmtPtr>& stmts)
   {
      for (auto& s : stmts)
      {
         if (!s) continue;
         if (s->rvalueExpr) s->rvalueExpr = FoldConstants(s->rvalueExpr);
         if (s->attribInitExpr) s->attribInitExpr = FoldConstants(s->attribInitExpr);
         if (s->expr) s->expr = FoldConstants(s->expr);
         if (s->ifCond) s->ifCond = FoldConstants(s->ifCond);
         FoldConstantsInStmts(s->thenStmts);
         FoldConstantsInStmts(s->elseStmts);
         FoldConstantsInStmts(s->forBody);
      }
   }

   bool CheckDataflowCycles(const std::vector<IRStmtPtr>& stmts,
                            const std::vector<DeclaredState>& states,
                            FieldError& outError)
   {
      // 1. Build directed graph
      std::vector<GraphNode> nodes;
      std::vector<std::vector<GraphEdge>> adj;

      auto addNode = [&](const std::string& name, SourceSpan sp, bool isRead = false, bool isWrite = false) -> int {
         int id = (int)nodes.size();
         nodes.push_back({ id, name, sp, isRead, isWrite });
         adj.push_back({});
         return id;
      };

      auto addEdge = [&](int from, int to, bool isDelay) {
         if (from >= 0 && from < (int)adj.size() && to >= 0 && to < (int)adj.size())
         {
            adj[from].push_back({ to, isDelay });
         }
      };

      std::unordered_map<std::string, int> stateReadNodes;
      std::unordered_map<std::string, int> stateWriteNodes;
      std::unordered_set<std::string> stateNames;

      for (const auto& st : states)
      {
         stateNames.insert(st.name);
         int rNode = addNode(st.name, st.span, true, false);
         int wNode = addNode(st.name, st.span, false, true);
         stateReadNodes[st.name] = rNode;
         stateWriteNodes[st.name] = wNode;
         // Unit delay back-edge: StateWrite -> StateRead
         addEdge(wNode, rNode, true);
      }

      // The graph is SSA-like and ORDER-AWARE: one node per assignment (not one per
      // name), and a read resolves to the most recent definition strictly before it.
      // Keying a node on the name alone made every read-modify-write its own self
      // edge, so the element domain's whole idiom - `P.y += sin(P.x * 2.0 + t) * 0.2`
      // - was reported as "dataflow cycle with no delay: P -> P". Within one kernel
      // invocation that is ordinary sequential dataflow; feedback across invocations
      // only exists through a state cell, which carries the delay back-edge above.
      std::unordered_map<const IRStmt*, int> stmtNodeMap;

      // Names that own storage at element-kernel entry: reading one before any
      // assignment in this body yields THIS element's input value, never a forward
      // reference to a later statement. Only plain locals can forward-reference.
      auto isEntryValued = [&](const std::string& n) -> bool {
         if (n == "P" || n == "N" || n == "uv" || n == "Cd" || n == "i" || n == "count" ||
             n == "t" || n == "dt" || n == "frame")
            return true;
         for (const auto& s : stmts)
         {
            if (s && s->kind == IRStmtKind::DeclAttrib && s->attribName == n)
               return true;
         }
         return false;
      };

      // name -> node id of the definition that a read at this point resolves to.
      // Seeded with each state cell's StateRead: that is its entry value.
      std::unordered_map<std::string, int> lastDef;
      for (const auto& st : states)
         lastDef[st.name] = stateReadNodes[st.name];

      // Definitions that appear strictly LATER than a given statement index. A read
      // with no prior definition that names a plain local defined further down IS a
      // genuine loop back to that definition - this is the case FIELDSTATETEST 2
      // depends on (`a = b + 1` / `b = a * 2`).
      std::unordered_map<std::string, std::vector<size_t>> defIndices;
      for (size_t k = 0; k < stmts.size(); ++k)
      {
         if (stmts[k] && stmts[k]->kind == IRStmtKind::Assign)
            defIndices[stmts[k]->assignTarget].push_back(k);
      }

      // Pre-create one graph node per assignment so a forward reference can point at it.
      std::vector<int> stmtNodeByIndex(stmts.size(), -1);
      for (size_t k = 0; k < stmts.size(); ++k)
      {
         if (!stmts[k]) continue;
         if (stmts[k]->kind == IRStmtKind::Assign)
         {
            int sNode = addNode(stmts[k]->assignTarget, stmts[k]->span);
            stmtNodeMap[stmts[k].get()] = sNode;
            stmtNodeByIndex[k] = sNode;
         }
      }

      for (size_t k = 0; k < stmts.size(); ++k)
      {
         const auto& stmt = stmts[k];
         if (!stmt) continue;

         if (stmt->kind == IRStmtKind::Assign)
         {
            int sNode = stmtNodeByIndex[k];
            std::vector<std::string> reads;
            if (stmt->rvalueExpr)
            {
               CollectReads(stmt->rvalueExpr, reads);
            }

            for (const auto& r : reads)
            {
               auto itPrev = lastDef.find(r);
               if (itPrev != lastDef.end())
               {
                  addEdge(itPrev->second, sNode, false);
                  continue;
               }
               if (isEntryValued(r))
                  continue; // input value for this element - not an edge

               auto itLater = defIndices.find(r);
               if (itLater != defIndices.end())
               {
                  for (size_t j : itLater->second)
                  {
                     if (j > k && stmtNodeByIndex[j] >= 0)
                     {
                        addEdge(stmtNodeByIndex[j], sNode, false);
                        break;
                     }
                  }
               }
            }

            // The definition becomes visible only to statements after this one.
            lastDef[stmt->assignTarget] = sNode;
         }
         else if (stmt->kind == IRStmtKind::StateWrite)
         {
            auto itW = stateWriteNodes.find(stmt->assignTarget);
            if (itW != stateWriteNodes.end())
            {
               auto itDef = lastDef.find(stmt->assignTarget);
               int srcNode = (itDef != lastDef.end()) ? itDef->second : stateReadNodes[stmt->assignTarget];
               addEdge(srcNode, itW->second, false);
            }
         }
      }

      int numNodes = (int)nodes.size();
      if (numNodes == 0) return true;

      // 2. Non-recursive Tarjan's Strongly Connected Components
      std::vector<int> dfn(numNodes, -1);
      std::vector<int> low(numNodes, -1);
      std::vector<bool> inStack(numNodes, false);
      std::vector<int> stk;
      int timer = 0;

      std::vector<std::vector<int>> sccs;

      // Iterative DFS stack
      struct Frame
      {
         int u;
         size_t edgeIdx;
      };

      for (int i = 0; i < numNodes; ++i)
      {
         if (dfn[i] != -1) continue;

         std::stack<Frame> callStack;
         dfn[i] = low[i] = ++timer;
         stk.push_back(i);
         inStack[i] = true;
         callStack.push({ i, 0 });

         while (!callStack.empty())
         {
            Frame& top = callStack.top();
            int u = top.u;

            if (top.edgeIdx < adj[u].size())
            {
               int v = adj[u][top.edgeIdx].to;
               top.edgeIdx++;

               if (dfn[v] == -1)
               {
                  dfn[v] = low[v] = ++timer;
                  stk.push_back(v);
                  inStack[v] = true;
                  callStack.push({ v, 0 });
               }
               else if (inStack[v])
               {
                  low[u] = std::min(low[u], dfn[v]);
               }
            }
            else
            {
               // Finished node u
               callStack.pop();
               if (!callStack.empty())
               {
                  int parent = callStack.top().u;
                  low[parent] = std::min(low[parent], low[u]);
               }

               if (dfn[u] == low[u])
               {
                  std::vector<int> scc;
                  while (true)
                  {
                     int v = stk.back();
                     stk.pop_back();
                     inStack[v] = false;
                     scc.push_back(v);
                     if (v == u) break;
                  }
                  sccs.push_back(std::move(scc));
               }
            }
         }
      }

      // 3. Check each SCC
      for (const auto& scc : sccs)
      {
         std::unordered_set<int> sccSet(scc.begin(), scc.end());
         bool hasSelfEdge = false;
         bool hasDelay = false;

         for (int u : scc)
         {
            for (const auto& e : adj[u])
            {
               if (sccSet.count(e.to))
               {
                  if (e.to == u) hasSelfEdge = true;
                  if (e.isDelay) hasDelay = true;
               }
            }
         }

         bool isCycle = (scc.size() > 1) || (scc.size() == 1 && hasSelfEdge);
         if (!isCycle || hasDelay)
            continue;

         // Found illegal SCC without delay!
         // 4. DFS inside this SCC to recover concrete cycle in source order
         int startNode = scc[0];
         // Pick earliest node by source span
         for (int u : scc)
         {
            if (nodes[u].span.line < nodes[startNode].span.line ||
                (nodes[u].span.line == nodes[startNode].span.line && nodes[u].span.col < nodes[startNode].span.col))
            {
               startNode = u;
            }
         }

         std::vector<int> cyclePath;
         std::vector<int> path;
         std::vector<bool> visited(numNodes, false);

         auto dfsCycle = [&](int curr, auto& self) -> bool {
            path.push_back(curr);
            visited[curr] = true;

            for (const auto& e : adj[curr])
            {
               if (!sccSet.count(e.to) || e.isDelay) continue;
               if (e.to == startNode && path.size() >= 1)
               {
                  path.push_back(startNode);
                  cyclePath = path;
                  return true;
               }
               if (!visited[e.to])
               {
                  if (self(e.to, self)) return true;
               }
            }

            path.pop_back();
            visited[curr] = false;
            return false;
         };

         if (!dfsCycle(startNode, dfsCycle))
         {
            cyclePath = scc;
            cyclePath.push_back(startNode);
         }

         // Format error message according to §5.2 contract:
         // error: dataflow cycle with no delay
         //     a  (line 3, col 1)
         //  -> b  (line 4, col 1)
         //  -> a  (line 3, col 1)
         //   hint: every cycle must pass through a state cell, which is a unit delay.
         //         Declare one on an edge of this cycle, e.g. `state float b = 0`
         std::ostringstream ss;
         ss << "dataflow cycle with no delay\n";

         std::string hintVar = nodes[startNode].name;
         for (size_t i = 0; i < cyclePath.size(); ++i)
         {
            const auto& n = nodes[cyclePath[i]];
            if (i == 0)
            {
               ss << "    " << n.name << "  (line " << n.span.line << ", col " << n.span.col << ")\n";
            }
            else
            {
               ss << " -> " << n.name << "  (line " << n.span.line << ", col " << n.span.col << ")\n";
               if (hintVar.empty() || hintVar == "out")
               {
                  hintVar = n.name;
               }
            }
         }

         ss << "  hint: every cycle must pass through a state cell, which is a unit delay.\n";
         ss << "        Declare one on an edge of this cycle, e.g. `state float " << hintVar << " = 0`";

         outError.severity = Severity::Error;
         outError.message = ss.str();
         outError.span = nodes[startNode].span;
         return false;
      }

      return true;
   }
}
