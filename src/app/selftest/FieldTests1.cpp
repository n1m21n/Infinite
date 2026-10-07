// Field self-tests, part 1 (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
// ============================================================ INFINITE_FIELDTEST
//
// Headless regression harness for the Field language pipeline and Expression::Evaluate.
// Runs Section A (Corpus), Section B (Lexer), Section C (Spans), Section D (Types).
// Gated as an early exit before glfwInit().
int RunFieldTest()
{
   printf("[FIELDTEST] Running Field regression harness...\n");

   // Attempt to open tests/field/corpus.txt from working directory or relative paths
   std::vector<std::string> candidatePaths = {
      "tests/field/corpus.txt",
      "../tests/field/corpus.txt",
      "/Users/namansoni/infinte/tests/field/corpus.txt"
   };

   std::ifstream file;
   std::string foundPath;
   for (const auto& p : candidatePaths)
   {
      file.open(p);
      if (file.is_open())
      {
         foundPath = p;
         break;
      }
   }

   if (!file.is_open())
   {
      printf("SECTION A (Corpus): FAIL - could not open tests/field/corpus.txt\n");
      printf("INFINITE_FIELDTEST: FAIL\n");
      return 1;
   }

   auto trim = [](const std::string& s) -> std::string {
      size_t start = s.find_first_not_of(" \t\r\n");
      if (start == std::string::npos) return "";
      size_t end = s.find_last_not_of(" \t\r\n");
      return s.substr(start, end - start + 1);
   };

   std::string line;
   int lineNum = 0;
   int casesTested = 0;
   int casesFailed = 0;

   while (std::getline(file, line))
   {
      lineNum++;
      std::string trimmed = trim(line);
      if (trimmed.empty() || trimmed[0] == '#')
         continue;

      // Parse fields separated by " | "
      std::vector<std::string> tokens;
      size_t pos = 0;
      while (pos < line.size())
      {
         size_t nextSep = line.find(" | ", pos);
         if (nextSep == std::string::npos)
         {
            tokens.push_back(trim(line.substr(pos)));
            break;
         }
         tokens.push_back(trim(line.substr(pos, nextSep - pos)));
         pos = nextSep + 3;
      }

      if (tokens.size() < 7)
      {
         printf("SECTION A (Corpus): FAIL - line %d malformed record: '%s'\n", lineNum, line.c_str());
         casesFailed++;
         continue;
      }

      std::string expr = tokens[0];
      double t = std::atof(tokens[1].c_str());
      std::string sibStr = tokens[2];
      std::string globStr = tokens[3];
      double expectVal = std::atof(tokens[4].c_str());
      int expectOk = std::atoi(tokens[5].c_str());
      std::string expectErrSubstr = tokens[6];

      std::map<std::string, float> sibMap;
      if (sibStr != "none" && !sibStr.empty())
      {
         std::stringstream ss(sibStr);
         std::string item;
         while (std::getline(ss, item, ','))
         {
            size_t eq = item.find('=');
            if (eq != std::string::npos)
            {
               std::string k = trim(item.substr(0, eq));
               float v = (float)std::atof(trim(item.substr(eq + 1)).c_str());
               sibMap[k] = v;
            }
         }
      }

      std::map<std::string, float> globMap;
      if (globStr != "none" && !globStr.empty())
      {
         std::stringstream ss(globStr);
         std::string item;
         while (std::getline(ss, item, ','))
         {
            size_t eq = item.find('=');
            if (eq != std::string::npos)
            {
               std::string k = trim(item.substr(0, eq));
               float v = (float)std::atof(trim(item.substr(eq + 1)).c_str());
               globMap[k] = v;
            }
         }
      }

      float outVal = 0.0f;
      std::string outErr;
      bool ok = Expression::Evaluate(expr, t,
                                     sibMap.empty() ? nullptr : &sibMap,
                                     globMap.empty() ? nullptr : &globMap,
                                     outVal, outErr);

      casesTested++;
      if (ok != (expectOk != 0))
      {
         printf("SECTION A (Corpus): FAIL - line %d expr '%s' expected ok=%d, got ok=%d (err: '%s')\n",
                lineNum, expr.c_str(), expectOk, (int)ok, outErr.c_str());
         casesFailed++;
         continue;
      }

      if (expectOk)
      {
         float expectedF = (float)expectVal;
         float diff = std::fabs(outVal - expectedF);
         if (diff > 1e-4f && std::fabs(diff / (std::fabs(expectedF) + 1e-5f)) > 1e-4f)
         {
            printf("SECTION A (Corpus): FAIL - line %d expr '%s' value mismatch: expected %.7g, got %.7g\n",
                   lineNum, expr.c_str(), expectedF, outVal);
            casesFailed++;
            continue;
         }
      }
      else
      {
         if (expectErrSubstr != "none" && outErr.find(expectErrSubstr) == std::string::npos)
         {
            printf("SECTION A (Corpus): FAIL - line %d expr '%s' error substring mismatch: expected '%s', got '%s'\n",
                   lineNum, expr.c_str(), expectErrSubstr.c_str(), outErr.c_str());
            casesFailed++;
            continue;
         }
      }
   }

   bool secAOk = (casesFailed == 0 && casesTested > 0);
   if (secAOk)
      printf("SECTION A (Corpus): OK (%d/%d passed)\n", casesTested, casesTested);
   else
      printf("SECTION A (Corpus): FAIL (%d failures out of %d cases)\n", casesFailed, casesTested);

   // Section B: Lexer Maximal Munch & Token Checks
   bool secBOk = true;
   {
      std::vector<std::string> ops = { "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=" };
      for (const auto& op : ops)
      {
         std::vector<Field::Token> toks;
         Field::FieldError err;
         if (!Field::Lex(op, toks, err) || toks.size() != 2 || toks[0].text != op || toks[0].kind != Field::TokenKind::Op)
         {
            printf("SECTION B (Lexer): FAIL - maximal munch failed for '%s'\n", op.c_str());
            secBOk = false;
         }
      }
   }
   if (secBOk)
      printf("SECTION B (Lexer): OK\n");
   else
      printf("SECTION B (Lexer): FAIL\n");

   // Section C: Source Spans
   bool secCOk = true;
   {
      std::vector<Field::Token> toks;
      Field::FieldError err;
      Field::Lex("a +\n  b", toks, err);
      // 'b' should be at line 2, col 3
      bool foundB = false;
      for (const auto& t : toks)
      {
         if (t.text == "b")
         {
            foundB = true;
            if (t.span.line != 2 || t.span.col != 3)
            {
               printf("SECTION C (Spans): FAIL - 'b' span expected line 2 col 3, got line %d col %d\n", t.span.line, t.span.col);
               secCOk = false;
            }
         }
      }
      if (!foundB)
      {
         printf("SECTION C (Spans): FAIL - token 'b' not found\n");
         secCOk = false;
      }
   }
   if (secCOk)
      printf("SECTION C (Spans): OK\n");
   else
      printf("SECTION C (Spans): FAIL\n");

   // Section D: Types, Vectors & Rank Polymorphism (Step 3)
   bool secDOk = true;
   {
      // 1. JoinRank Unit Tests
      Field::FieldType resType;
      std::string rankErr;
      if (!Field::JoinRank(Field::FieldType(Field::DataType::Float, 1), Field::FieldType(Field::DataType::Vec2, 2), resType, rankErr) || resType.kind != Field::DataType::Vec2 || resType.lanes != 2)
      {
         printf("SECTION D (Types): FAIL - scalar + vec2 rank join failed\n");
         secDOk = false;
      }
      if (!Field::JoinRank(Field::FieldType(Field::DataType::Vec3, 3), Field::FieldType(Field::DataType::Float, 1), resType, rankErr) || resType.kind != Field::DataType::Vec3 || resType.lanes != 3)
      {
         printf("SECTION D (Types): FAIL - vec3 + scalar rank join failed\n");
         secDOk = false;
      }
      if (!Field::JoinRank(Field::FieldType(Field::DataType::Vec4, 4), Field::FieldType(Field::DataType::Vec4, 4), resType, rankErr) || resType.kind != Field::DataType::Vec4 || resType.lanes != 4)
      {
         printf("SECTION D (Types): FAIL - vec4 + vec4 rank join failed\n");
         secDOk = false;
      }
      if (Field::JoinRank(Field::FieldType(Field::DataType::Vec2, 2), Field::FieldType(Field::DataType::Vec3, 3), resType, rankErr))
      {
         printf("SECTION D (Types): FAIL - vec2 + vec3 should be refused\n");
         secDOk = false;
      }
      if (Field::JoinRank(Field::FieldType(Field::DataType::Vec4, 4), Field::FieldType(Field::DataType::Vec3, 3), resType, rankErr))
      {
         printf("SECTION D (Types): FAIL - vec4 + vec3 should be refused\n");
         secDOk = false;
      }

      // 2. Swizzle Validation
      Field::SwizzleInfo swInfo;
      std::string swErr;
      if (!Field::ParseAndValidateSwizzle("xz", Field::FieldType(Field::DataType::Vec3, 3), "P", swInfo, swErr) ||
          swInfo.numComponents != 2 || swInfo.indices[0] != 0 || swInfo.indices[1] != 2)
      {
         printf("SECTION D (Types): FAIL - .xz on vec3 failed\n");
         secDOk = false;
      }
      if (!Field::ParseAndValidateSwizzle("bgr", Field::FieldType(Field::DataType::Vec3, 3), "Cd", swInfo, swErr) ||
          swInfo.numComponents != 3 || swInfo.indices[0] != 2 || swInfo.indices[1] != 1 || swInfo.indices[2] != 0)
      {
         printf("SECTION D (Types): FAIL - .bgr on vec3 failed\n");
         secDOk = false;
      }
      if (!Field::ParseAndValidateSwizzle("xxx", Field::FieldType(Field::DataType::Vec3, 3), "P", swInfo, swErr) ||
          swInfo.numComponents != 3 || swInfo.indices[0] != 0 || swInfo.indices[1] != 0 || swInfo.indices[2] != 0)
      {
         printf("SECTION D (Types): FAIL - .xxx on vec3 failed\n");
         secDOk = false;
      }
      if (Field::ParseAndValidateSwizzle("xg", Field::FieldType(Field::DataType::Vec3, 3), "P", swInfo, swErr))
      {
         printf("SECTION D (Types): FAIL - mixed swizzle .xg should be refused\n");
         secDOk = false;
      }
      if (Field::ParseAndValidateSwizzle("w", Field::FieldType(Field::DataType::Vec3, 3), "P", swInfo, swErr))
      {
         printf("SECTION D (Types): FAIL - .w on vec3 should be refused\n");
         secDOk = false;
      }
      if (Field::ParseAndValidateSwizzle("x", Field::FieldType(Field::DataType::Float, 1), "t", swInfo, swErr))
      {
         printf("SECTION D (Types): FAIL - .x on scalar float should be refused\n");
         secDOk = false;
      }

      // 3. VM Vector Execution Helper
      auto evalVec = [](const std::string& src, Field::VectorResult& outVr, std::string& outErr) -> bool {
         std::vector<Field::Token> toks;
         Field::FieldError fErr;
         if (!Field::Lex(src, toks, fErr)) { outErr = fErr.message; return false; }
         Field::AstNodePtr ast;
         if (!Field::ParseExpression(toks, ast, fErr)) { outErr = fErr.message; return false; }
         Field::IRNodePtr ir;
         if (!Field::LowerAstToIR(ast, ir, fErr)) { outErr = fErr.message; return false; }
         Field::BytecodeProgram prog;
         if (!Field::EmitBytecode(ir, prog, fErr)) { outErr = fErr.message; return false; }
         Field::FieldVM vm;
         Field::ExecutionEnv env;
         return vm.ExecuteVector(prog, env, outVr, outErr);
      };

      // Constructors & Broadcast
      {
         Field::VectorResult vr;
         std::string err;
         if (!evalVec("vec3(1)", vr, err) || vr.lanes != 3 || vr.v[0] != 1.0 || vr.v[1] != 1.0 || vr.v[2] != 1.0)
         {
            printf("SECTION D (Types): FAIL - vec3(1) splat failed\n");
            secDOk = false;
         }
         if (!evalVec("vec3(1, 2, 3)", vr, err) || vr.lanes != 3 || vr.v[0] != 1.0 || vr.v[1] != 2.0 || vr.v[2] != 3.0)
         {
            printf("SECTION D (Types): FAIL - vec3(1, 2, 3) full ctor failed\n");
            secDOk = false;
         }
         if (!evalVec("vec4(vec2(1, 2), vec2(3, 4))", vr, err) || vr.lanes != 4 || vr.v[0] != 1.0 || vr.v[1] != 2.0 || vr.v[2] != 3.0 || vr.v[3] != 4.0)
         {
            printf("SECTION D (Types): FAIL - vec4(vec2, vec2) mixed ctor failed\n");
            secDOk = false;
         }
         if (!evalVec("vec4(vec3(1, 2, 3), 4)", vr, err) || vr.lanes != 4 || vr.v[0] != 1.0 || vr.v[1] != 2.0 || vr.v[2] != 3.0 || vr.v[3] != 4.0)
         {
            printf("SECTION D (Types): FAIL - vec4(vec3, 4) mixed ctor failed\n");
            secDOk = false;
         }
         if (evalVec("vec3(vec2(1, 2))", vr, err))
         {
            printf("SECTION D (Types): FAIL - vec3(vec2) should be refused\n");
            secDOk = false;
         }
         if (evalVec("vec3(vec2(1, 2), vec2(3, 4))", vr, err))
         {
            printf("SECTION D (Types): FAIL - vec3(vec2, vec2) lane sum 4 should be refused\n");
            secDOk = false;
         }
      }

      // Arithmetic, Power, Swizzle Chaining & Precision
      {
         Field::VectorResult vr;
         std::string err;
         if (!evalVec("vec3(1, 2, 3) * 2", vr, err) || vr.lanes != 3 || vr.v[0] != 2.0 || vr.v[1] != 4.0 || vr.v[2] != 6.0)
         {
            printf("SECTION D (Types): FAIL - vec3 * 2 broadcast failed\n");
            secDOk = false;
         }
         if (!evalVec("vec3(1) + 0.5", vr, err) || vr.lanes != 3 || vr.v[0] != 1.5 || vr.v[1] != 1.5 || vr.v[2] != 1.5)
         {
            printf("SECTION D (Types): FAIL - vec3(1) + 0.5 failed\n");
            secDOk = false;
         }
         if (!evalVec("vec3(1, 2, 3) ^ 2", vr, err) || vr.lanes != 3 || vr.v[0] != 1.0 || vr.v[1] != 4.0 || vr.v[2] != 9.0)
         {
            printf("SECTION D (Types): FAIL - vec3 ^ 2 power failed\n");
            secDOk = false;
         }
         if (!evalVec("vec3(10, 20, 30).xz", vr, err) || vr.lanes != 2 || vr.v[0] != 10.0 || vr.v[1] != 30.0)
         {
            printf("SECTION D (Types): FAIL - .xz swizzle read failed\n");
            secDOk = false;
         }
         if (!evalVec("vec3(10, 20, 30).xy.y", vr, err) || vr.lanes != 1 || vr.v[0] != 20.0)
         {
            printf("SECTION D (Types): FAIL - .xy.y swizzle chain failed\n");
            secDOk = false;
         }
         // Double precision across all lanes
         if (!evalVec("vec3(0.1) + vec3(0.2)", vr, err) || vr.lanes != 3 ||
             (float)vr.v[0] != 0.30000001192092896f || (float)vr.v[1] != 0.30000001192092896f || (float)vr.v[2] != 0.30000001192092896f)
         {
            printf("SECTION D (Types): FAIL - vector 0.1+0.2 double precision mismatch\n");
            secDOk = false;
         }
      }

      // Top-level vector refusal via Expression::Evaluate
      {
         float outVal = 123.456f;
         std::string errStr;
         bool res = Expression::Evaluate("vec3(1, 0, 0)", 0.0, nullptr, nullptr, outVal, errStr);
         if (res || outVal != 123.456f || errStr.find("expression has type vec3") == std::string::npos)
         {
            printf("SECTION D (Types): FAIL - top-level vec3 refusal failed (res=%d, outVal=%.3f, err='%s')\n", (int)res, outVal, errStr.c_str());
            secDOk = false;
         }
      }

      // Vector comparison refusal (Decision 1)
      {
         Field::VectorResult vr;
         std::string err;
         if (evalVec("vec3(1) > vec3(0)", vr, err) || err.find("requires scalar arguments") == std::string::npos)
         {
            printf("SECTION D (Types): FAIL - vector comparison should be refused (err='%s')\n", err.c_str());
            secDOk = false;
         }
      }
   }
   if (secDOk)
      printf("SECTION D (Types): OK\n");
   else
      printf("SECTION D (Types): FAIL\n");

   // Section E: Pure Randomness (Step 2)
   bool secEOk = true;
   {
      // 1. Non-negativity & Range [0, 1) across time and seed space
      uint64_t testSeeds[] = { 0, 1, 2, 7, 42, 1000, 0x12345678ULL, 0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL };
      for (uint64_t s : testSeeds)
      {
         for (double t = -300.0; t <= 300.0; t += 0.5)
         {
            double r = Field::TimeToRand(t, s);
            if (r < 0.0 || r >= 1.0)
            {
               printf("SECTION E (Random): FAIL - TimeToRand(%.1f, %llu) out of range: %.17g\n", t, (unsigned long long)s, r);
               secEOk = false;
               break;
            }
         }
         if (!secEOk) break;
      }

      // 2. Determinism / Reproducibility: same (t, seed) produces identical value
      for (int i = 0; i < 100; ++i)
      {
         double t = (double)i * 0.37;
         uint64_t s = (uint64_t)(i * 1337);
         double r1 = Field::TimeToRand(t, s);
         double r2 = Field::TimeToRand(t, s);
         if (r1 != r2)
         {
            printf("SECTION E (Random): FAIL - non-deterministic TimeToRand at t=%.2f, s=%llu\n", t, (unsigned long long)s);
            secEOk = false;
            break;
         }
      }

      // 3. Different seeds produce distinct values
      int seedCollisions = 0;
      for (int i = 0; i < 100; ++i)
      {
         double t = (double)i * 0.5;
         double rA = Field::TimeToRand(t, 0);
         double rB = Field::TimeToRand(t, 1);
         if (rA == rB)
            seedCollisions++;
      }
      if (seedCollisions > 0)
      {
         printf("SECTION E (Random): FAIL - different seeds collided %d times out of 100\n", seedCollisions);
         secEOk = false;
      }

      // 4. Decorrelation: adjacent frames at 60 fps have correlation < 0.20
      {
         const int N = 10000;
         const double dt = 1.0 / 60.0;
         double sumX = 0.0, sumY = 0.0;
         std::vector<double> xs(N), ys(N);
         for (int i = 0; i < N; ++i)
         {
            xs[i] = Field::TimeToRand((double)i * dt, 0);
            ys[i] = Field::TimeToRand((double)(i + 1) * dt, 0);
            sumX += xs[i];
            sumY += ys[i];
         }
         double meanX = sumX / N;
         double meanY = sumY / N;
         double cov = 0.0, varX = 0.0, varY = 0.0;
         for (int i = 0; i < N; ++i)
         {
            cov += (xs[i] - meanX) * (ys[i] - meanY);
            varX += (xs[i] - meanX) * (xs[i] - meanX);
            varY += (ys[i] - meanY) * (ys[i] - meanY);
         }
         double corr = cov / std::sqrt(varX * varY);
         if (std::fabs(corr) >= 0.20)
         {
            printf("SECTION E (Random): FAIL - 60fps correlation too high: %.4f (expected < 0.20)\n", corr);
            secEOk = false;
         }
      }

      // 5. Periodicity: exactly 300 seconds
      {
         double p1 = Field::TimeToRand(1.25, 7);
         double p2 = Field::TimeToRand(301.25, 7);
         double p3 = Field::TimeToRand(601.25, 7);
         if (p1 != p2 || p1 != p3)
         {
            printf("SECTION E (Random): FAIL - 300s period check failed (%.17g vs %.17g vs %.17g)\n", p1, p2, p3);
            secEOk = false;
         }
      }

      // 6. sh step boundaries: constant across step, changes at boundary
      {
         double shA = Field::Sh(0.0, 1.0, 4.0, 0.0, 0.1);
         double shB = Field::Sh(0.0, 1.0, 4.0, 0.0, 0.24);
         double shC = Field::Sh(0.0, 1.0, 4.0, 0.0, 0.26);
         if (shA != shB)
         {
            printf("SECTION E (Random): FAIL - sh changed within step interval [0.1, 0.24]\n");
            secEOk = false;
         }
         if (shA == shC)
         {
            printf("SECTION E (Random): FAIL - sh did not change across step boundary at t=0.25\n");
            secEOk = false;
         }
      }

      // 7. Arity check: 5 arguments produces expected error
      {
         float outV = 0.0f;
         std::string errStr;
         bool res = Expression::Evaluate("rand(0, 1, 2, 3, 4)", 0.0, nullptr, nullptr, outV, errStr);
         if (res || errStr.find("expects 0 to 4 arguments") == std::string::npos)
         {
            printf("SECTION E (Random): FAIL - 5-arg rand did not produce expected error (ok=%d, err='%s')\n", (int)res, errStr.c_str());
            secEOk = false;
         }
      }

      // 8. Constant folding prevention across t
      {
         float vA = 0.0f, vB = 0.0f;
         std::string errA, errB;
         bool okA = Expression::Evaluate("rand(0, 1, 2)", 1.0, nullptr, nullptr, vA, errA);
         bool okB = Expression::Evaluate("rand(0, 1, 2)", 2.0, nullptr, nullptr, vB, errB);
         if (!okA || !okB || vA == vB)
         {
            printf("SECTION E (Random): FAIL - cached rand program did not vary across t (vA=%.7g, vB=%.7g)\n", vA, vB);
            secEOk = false;
         }
      }
   }
   if (secEOk)
      printf("SECTION E (Pure Randomness): OK\n");
   else
      printf("SECTION E (Pure Randomness): FAIL\n");

   bool allOk = secAOk && secBOk && secCOk && secDOk && secEOk;
   printf("INFINITE_FIELDTEST: %s\n", allOk ? "OK" : "FAIL");
   return allOk ? 0 : 1;
}

// ============================================================ INFINITE_FIELDELEMENTTEST
//
// Headless regression harness for the Field element domain.
// Guards the SoA store, round-trip fidelity, rate inference hoisting,
// reserved-word shadowing, undeclared attribute refusals, and geometry passthrough.
int RunFieldElementTest()
{
   printf("[FIELDELEMENTTEST] Running Field element-domain conformance harness...\n");
   bool allOk = true;

   // 1. AoS / SoA Round-Trip & Preservation (empty-stays-empty)
   {
      bool secOk = true;
      Mesh srcMesh;
      for (int i = 0; i < 5; ++i)
      {
         Vertex v;
         v.px = (float)i * 1.0f; v.py = (float)i * 2.0f; v.pz = (float)i * 3.0f;
         v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
         v.u = (float)i * 0.25f; v.v = 1.0f - (float)i * 0.25f;
         srcMesh.vertices.push_back(v);
      }
      srcMesh.indices = { 0, 1, 2, 2, 3, 4 };
      srcMesh.faceMask = { 1, 0 };
      srcMesh.selectionGroup = { 10, 20 };

      Field::ElementStore store;
      store.FromMesh(srcMesh);

      if (store.Count() != 5) secOk = false;
      if (store.HasInputVertexColor()) secOk = false; // empty input

      Mesh outMesh;
      Field::MeshWriteMask noWrites;
      store.ToMesh(outMesh, noWrites, srcMesh);

      if (outMesh.vertices.size() != 5) secOk = false;
      for (size_t i = 0; i < 5; ++i)
      {
         if (outMesh.vertices[i].px != srcMesh.vertices[i].px ||
             outMesh.vertices[i].py != srcMesh.vertices[i].py ||
             outMesh.vertices[i].pz != srcMesh.vertices[i].pz ||
             outMesh.vertices[i].nx != srcMesh.vertices[i].nx ||
             outMesh.vertices[i].ny != srcMesh.vertices[i].ny ||
             outMesh.vertices[i].nz != srcMesh.vertices[i].nz ||
             outMesh.vertices[i].u != srcMesh.vertices[i].u ||
             outMesh.vertices[i].v != srcMesh.vertices[i].v)
         {
            secOk = false;
         }
      }

      if (outMesh.indices != srcMesh.indices) secOk = false;
      if (outMesh.faceMask != srcMesh.faceMask) secOk = false;
      if (outMesh.selectionGroup != srcMesh.selectionGroup) secOk = false;

      // Trap #5: empty vertexColor must remain empty
      if (!outMesh.vertexColor.empty())
      {
         printf("AoS/SoA: FAIL - empty vertexColor was converted to non-empty array!\n");
         secOk = false;
      }

      // Test with non-empty vertexColor
      srcMesh.vertexColor = { 1, 0, 0,  0, 1, 0,  0, 0, 1,  1, 1, 0,  0, 1, 1 };
      store.FromMesh(srcMesh);
      if (!store.HasInputVertexColor()) secOk = false;
      store.ToMesh(outMesh, noWrites, srcMesh);
      if (outMesh.vertexColor != srcMesh.vertexColor)
      {
         printf("AoS/SoA: FAIL - non-empty vertexColor was not preserved!\n");
         secOk = false;
      }

      if (secOk) printf("SECTION 1 (AoS/SoA Round Trip): OK\n");
      else { printf("SECTION 1 (AoS/SoA Round Trip): FAIL\n"); allOk = false; }
   }

   // 2. N-element Kernel execution counter and count variable
   {
      bool secOk = true;
      Mesh mesh;
      const int N = 64;
      for (int i = 0; i < N; ++i)
      {
         Vertex v;
         v.px = 0; v.py = 0; v.pz = 0;
         mesh.vertices.push_back(v);
      }

      std::string code = "P.x = count\nP.y = i\n";
      FieldElementNode node;
      node.code = code;
      if (!node.Apply())
      {
         printf("N-element: FAIL - failed to compile '%s': %s\n", code.c_str(), node.LastError().c_str());
         secOk = false;
      }
      else
      {
         Field::ElementStore store;
         store.FromMesh(mesh);
         Field::ElementVM vm;
         Field::ExecutionEnv env;
         std::string err;
         node.Program()->ResetCounters();
         vm.Execute(*node.Program(), store, env, err);

         if (node.Program()->elementEvalCount != N)
         {
            printf("N-element: FAIL - element loop ran %llu times (expected %d)\n", (unsigned long long)node.Program()->elementEvalCount, N);
            secOk = false;
         }

         Mesh outMesh;
         store.ToMesh(outMesh, node.Program()->WriteMask(), mesh);
         for (int i = 0; i < N; ++i)
         {
            if (outMesh.vertices[i].px != (float)N || outMesh.vertices[i].py != (float)i)
            {
               printf("N-element: FAIL - ramp check failed at i=%d: px=%.2f (expected %d), py=%.2f (expected %d)\n",
                      i, outMesh.vertices[i].px, N, outMesh.vertices[i].py, i);
               secOk = false;
               break;
            }
         }
      }

      if (secOk) printf("SECTION 2 (N-Element Loop & Count): OK\n");
      else { printf("SECTION 2 (N-Element Loop & Count): FAIL\n"); allOk = false; }
   }

   // 3. Hoisting: Frame-domain subexpression runs ONCE per cook on a counter
   {
      bool secOk = true;
      const int N = 100;
      Mesh mesh;
      for (int i = 0; i < N; ++i) mesh.vertices.push_back(Vertex{});

      std::string code = "amount = 0.5 + 0.5 * sin(t)\nP.y += amount\n";
      FieldElementNode node;
      node.code = code;
      if (!node.Apply())
      {
         printf("Hoisting: FAIL - failed to compile: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         Field::ElementStore store;
         store.FromMesh(mesh);
         Field::ElementVM vm;
         Field::ExecutionEnv env;
         env.t = 1.5;
         std::string err;
         node.Program()->ResetCounters();
         vm.Execute(*node.Program(), store, env, err);

         if (node.Program()->prologueEvalCount != 1)
         {
            printf("Hoisting: FAIL - prologue ran %llu times (expected exactly 1)\n", (unsigned long long)node.Program()->prologueEvalCount);
            secOk = false;
         }
         if (node.Program()->elementEvalCount != N)
         {
            printf("Hoisting: FAIL - element loop ran %llu times (expected %d)\n", (unsigned long long)node.Program()->elementEvalCount, N);
            secOk = false;
         }

         float expectedAmount = 0.5f + 0.5f * std::sin(1.5f);
         for (int i = 0; i < N; ++i)
         {
            if (std::fabs(store.Py()[i] - expectedAmount) > 1e-5f)
            {
               printf("Hoisting: FAIL - P.y value mismatch at %d: %.6f vs %.6f\n", i, store.Py()[i], expectedAmount);
               secOk = false;
               break;
            }
         }
      }

      if (secOk) printf("SECTION 3 (Rate Inference & Hoisting): OK\n");
      else { printf("SECTION 3 (Rate Inference & Hoisting): FAIL\n"); allOk = false; }
   }

   // 4. Attrib Declarations & Usage
   {
      bool secOk = true;
      Mesh mesh;
      const int N = 32;
      for (int i = 0; i < N; ++i)
      {
         Vertex v;
         v.px = (float)i;
         mesh.vertices.push_back(v);
      }

      std::string code = "attrib float heat = 0\nheat = P.x * 0.1\nCd = vec3(heat, 0.0, 1.0 - heat)\n";
      FieldElementNode node;
      node.code = code;
      if (!node.Apply())
      {
         printf("Attrib: FAIL - failed to compile: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         Field::ElementStore store;
         store.DeclareAttrib("heat", Field::DataType::Float);
         store.FromMesh(mesh);
         Field::ElementVM vm;
         Field::ExecutionEnv env;
         std::string err;
         vm.Execute(*node.Program(), store, env, err);

         Mesh outMesh;
         store.ToMesh(outMesh, node.Program()->WriteMask(), mesh);
         if (!outMesh.HasVertexColor())
         {
            printf("Attrib: FAIL - Cd write did not produce vertexColor\n");
            secOk = false;
         }
         else
         {
            for (int i = 0; i < N; ++i)
            {
               float h = (float)i * 0.1f;
               if (std::fabs(outMesh.vertexColor[3 * i + 0] - h) > 1e-5f ||
                   std::fabs(outMesh.vertexColor[3 * i + 2] - (1.0f - h)) > 1e-5f)
               {
                  printf("Attrib: FAIL - Cd values mismatch at i=%d\n", i);
                  secOk = false;
                  break;
               }
            }
         }
      }

      if (secOk) printf("SECTION 4 (User Attribs): OK\n");
      else { printf("SECTION 4 (User Attribs): FAIL\n"); allOk = false; }
   }

   // 5. Refusals: Shadowing reserved words, Undeclared attrib, Non-constant loop bound
   {
      bool secOk = true;

      // 5a. Reserved-word shadowing
      {
         FieldElementNode node;
         node.code = "attrib float P = 0\n";
         bool ok = node.Apply();
         if (ok || node.LastError().find("element") == std::string::npos)
         {
            printf("Refusal (Shadowing): FAIL - 'attrib float P' did not fail with element domain message (ok=%d, err='%s')\n", (int)ok, node.LastError().c_str());
            secOk = false;
         }
      }

      // 5b. Frame reserved word shadowing
      {
         FieldElementNode node;
         node.code = "attrib float t = 0\n";
         bool ok = node.Apply();
         if (ok || node.LastError().find("frame") == std::string::npos)
         {
            printf("Refusal (Shadowing frame): FAIL - 'attrib float t' did not fail with frame domain message (ok=%d, err='%s')\n", (int)ok, node.LastError().c_str());
            secOk = false;
         }
      }

      // 5c. Undeclared attribute error at use site line and column
      {
         FieldElementNode node;
         node.code = "P.y += 1.0\nheat += 0.5\n";
         bool ok = node.Apply();
         if (ok || node.LastError().find("undeclared") == std::string::npos ||
             node.LastError().find("line 2") == std::string::npos)
         {
            printf("Refusal (Undeclared): FAIL - 'heat += 0.5' without decl did not error with line 2 (ok=%d, err='%s')\n", (int)ok, node.LastError().c_str());
            secOk = false;
         }
      }

      // 5d. Non-constant loop bound refused
      {
         FieldElementNode node;
         node.code = "for (k = 0; k < count; k += 1) { P.y += 0.1 }\n";
         bool ok = node.Apply();
         if (ok || node.LastError().find("loop bound must be a compile-time constant") == std::string::npos)
         {
            printf("Refusal (Non-const loop): FAIL - for loop with 'count' bound did not error (ok=%d, err='%s')\n", (int)ok, node.LastError().c_str());
            secOk = false;
         }
      }

      // 5e. Read-only assignment refused
      {
         FieldElementNode node;
         node.code = "i = 5\n";
         bool ok = node.Apply();
         if (ok || node.LastError().find("read-only") == std::string::npos)
         {
            printf("Refusal (Read-only i): FAIL - assignment to 'i' did not error (ok=%d, err='%s')\n", (int)ok, node.LastError().c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 5 (Refusals & Diagnostics): OK\n");
      else { printf("SECTION 5 (Refusals & Diagnostics): FAIL\n"); allOk = false; }
   }

   // 6. Element Count Ceiling & Truncation
   {
      bool secOk = true;
      GeometryNode geo;
      geo.sides = 20;
      geo.CookIfNeeded(1);
      const Mesh& geoMesh = geo.GetMesh();
      size_t origCount = geoMesh.vertices.size();

      FieldElementNode fe;
      fe.input = &geo;
      fe.maxElements = 10;
      fe.code = "P.y += 0.5\n";
      fe.Apply();
      fe.CookIfNeeded(2);

      const Mesh& outM = fe.GetMesh();
      if (outM.vertices.size() != 10)
      {
         printf("Truncation: FAIL - output mesh size is %zu (expected capped 10, input was %zu)\n", outM.vertices.size(), origCount);
         secOk = false;
      }
      if (!fe.WasTruncated())
      {
         printf("Truncation: FAIL - fe.WasTruncated() is false\n");
         secOk = false;
      }
      if (fe.ActualElementCount() != 10)
      {
         printf("Truncation: FAIL - ActualElementCount is %d (expected 10)\n", fe.ActualElementCount());
         secOk = false;
      }

      if (secOk) printf("SECTION 6 (Element Cap & Truncation): OK\n");
      else { printf("SECTION 6 (Element Cap & Truncation): FAIL\n"); allOk = false; }
   }

   // 7. Geometry Passthrough Forwarding
   {
      bool secOk = true;
      GeometryNode geo;
      geo.CookIfNeeded(1);

      FieldElementNode fe;
      fe.input = &geo;
      fe.code = "P.x += 0.0\n";
      fe.Apply();
      fe.CookIfNeeded(2);

      if (fe.PassthroughSource() != &geo)
      {
         printf("Passthrough: FAIL - PassthroughSource did not return input geo\n");
         secOk = false;
      }
      if (fe.GetModelMatrix() != geo.GetModelMatrix())
      {
         printf("Passthrough: FAIL - GetModelMatrix was not forwarded\n");
         secOk = false;
      }
      if (fe.BypassSource() != dynamic_cast<INode*>(&geo))
      {
         printf("Passthrough: FAIL - BypassSource was not forwarded\n");
         secOk = false;
      }

      if (secOk) printf("SECTION 7 (Geometry Passthrough): OK\n");
      else { printf("SECTION 7 (Geometry Passthrough): FAIL\n"); allOk = false; }
   }

   // 8. Every shipped program compiles: the default constructor's code and all five
   //    presets. The node's own default (`P.y += sin(P.x * 2.0 + t) * 0.2`) and two of
   //    the presets were rejected as "dataflow cycle with no delay: P -> P", so
   //    spawning the node showed a compile error and passed geometry through untouched.
   //    Read-modify-write of an element attribute is sequential dataflow, not feedback.
   {
      bool secOk = true;

      {
         FieldElementNode fresh;
         if (!fresh.LastError().empty())
         {
            printf("Presets: FAIL - default program did not compile: %s\n", fresh.LastError().c_str());
            secOk = false;
         }
         if (!fresh.Program())
         {
            printf("Presets: FAIL - default program produced no bytecode\n");
            secOk = false;
         }
      }

      for (const auto& preset : FieldElementNode::Presets())
      {
         FieldElementNode node;
         node.code = preset.code;
         if (!node.Apply())
         {
            printf("Presets: FAIL - preset '%s' did not compile: %s\n", preset.name, node.LastError().c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 8 (Default Program & Presets Compile): OK\n");
      else { printf("SECTION 8 (Default Program & Presets Compile): FAIL\n"); allOk = false; }
   }

   // 9. Same expression, both domains, same answer. The prologue and the element loop
   //    are two evaluations of the same opcode set; anything one can compute the other
   //    must compute identically. This table is the assertion that catches an opcode
   //    implemented in one bank and missing from the other - which is how the prologue
   //    shipped with no comparison, logical, unary or branch opcode at all, silently
   //    returning 0 for `k = t > 1.0` while the element loop returned 1.
   {
      bool secOk = true;

      struct DomainPair
      {
         const char* name;
         const char* frameCode;   // expression hoisted to the prologue via a bare local
         const char* elemCode;    // same expression forced into the element loop
         double expected;
      };

      // env.t is 1.5 and every vertex starts at P = (0,0,0), so `P.x * 0.0` only pins
      // the statement to the element domain without perturbing the value.
      static const DomainPair kPairs[] = {
         { "neg",          "k = -t\nP.y += k\n",                          "P.y += -t + P.x * 0.0\n",                          -1.5 },
         { "not",          "k = !(t > 9.0)\nP.y += k\n",                  "P.y += !(t > 9.0) + P.x * 0.0\n",                   1.0 },
         { "less",         "k = t < 2.0\nP.y += k\n",                     "P.y += (t < 2.0) + P.x * 0.0\n",                    1.0 },
         { "lessEqual",    "k = t <= 1.5\nP.y += k\n",                    "P.y += (t <= 1.5) + P.x * 0.0\n",                   1.0 },
         { "greater",      "k = t > 1.0\nP.y += k\n",                     "P.y += (t > 1.0) + P.x * 0.0\n",                    1.0 },
         { "greaterEqual", "k = t >= 9.0\nP.y += k\n",                    "P.y += (t >= 9.0) + P.x * 0.0\n",                   0.0 },
         { "equal",        "k = t == 1.5\nP.y += k\n",                    "P.y += (t == 1.5) + P.x * 0.0\n",                   1.0 },
         { "notEqual",     "k = t != 1.5\nP.y += k\n",                    "P.y += (t != 1.5) + P.x * 0.0\n",                   0.0 },
         { "logicalAnd",   "k = (t > 1.0) && (t < 2.0)\nP.y += k\n",      "P.y += ((t > 1.0) && (t < 2.0)) + P.x * 0.0\n",     1.0 },
         { "logicalOr",    "k = (t > 9.0) || (t < 2.0)\nP.y += k\n",      "P.y += ((t > 9.0) || (t < 2.0)) + P.x * 0.0\n",     1.0 },
         { "mod",          "k = t % 1.0\nP.y += k\n",                     "P.y += (t % 1.0) + P.x * 0.0\n",                    0.5 },
         { "pow",          "k = t ^ 2.0\nP.y += k\n",                     "P.y += (t ^ 2.0) + P.x * 0.0\n",                    2.25 },
         { "builtinPow",   "k = pow(t, 2.0)\nP.y += k\n",                 "P.y += pow(t, 2.0) + P.x * 0.0\n",                  2.25 },
         { "builtinMod",   "k = mod(t, 1.0)\nP.y += k\n",                 "P.y += mod(t, 1.0) + P.x * 0.0\n",                  0.5 },
         { "builtinFloor", "k = floor(t)\nP.y += k\n",                    "P.y += floor(t) + P.x * 0.0\n",                     1.0 },
         { "builtinStep",  "k = step(1.0, t)\nP.y += k\n",                "P.y += step(1.0, t) + P.x * 0.0\n",                 1.0 },
         { "builtinClamp", "k = clamp(t, 0.0, 1.0)\nP.y += k\n",          "P.y += clamp(t, 0.0, 1.0) + P.x * 0.0\n",           1.0 },
         { "builtinMix",   "k = mix(0.0, t, 0.5)\nP.y += k\n",            "P.y += mix(0.0, t, 0.5) + P.x * 0.0\n",             0.75 },
         { "ifExpr",       "k = if(t > 1.0, 7.0, 0.0)\nP.y += k\n",       "P.y += if(t > 1.0, 7.0, 0.0) + P.x * 0.0\n",        7.0 },
         { "vecCtor",      "k = vec3(t, 0.0, 0.0).x\nP.y += k\n",         "P.y += vec3(t, 0.0, 0.0).x + P.x * 0.0\n",          1.5 },
         { "ifStmt",       "k = 0.0\nif (t > 1.0) { k = 5.0 }\nP.y += k\n",
                                                                          "if (t > 1.0) { P.y += 5.0 + P.x * 0.0 }\n",         5.0 },
         { "ifElseStmt",   "k = 0.0\nif (t > 9.0) { k = 1.0 } else { k = 3.0 }\nP.y += k\n",
                                                                          "if (t > 9.0) { P.y += 1.0 } else { P.y += 3.0 + P.x * 0.0 }\n", 3.0 },
         { "forLoop",      "k = 0.0\nfor (j = 0; j < 3; j += 1) { k += 2.0 }\nP.y += k\n",
                                                                          "for (j = 0; j < 3; j += 1) { P.y += 2.0 + P.x * 0.0 }\n", 6.0 },
      };

      const int N = 4;

      // Runs one program over a fresh N-vertex mesh at t = 1.5 and returns P.y[0].
      auto runCode = [&](const char* code, double& outValue, uint64_t& outPrologueRuns, std::string& outErr) -> bool
      {
         Mesh mesh;
         for (int i = 0; i < N; ++i) mesh.vertices.push_back(Vertex{});

         FieldElementNode node;
         node.code = code;
         if (!node.Apply() || !node.Program())
         {
            outErr = node.LastError();
            if (outErr.empty()) outErr = "no bytecode produced";
            return false;
         }

         Field::ElementStore store;
         store.FromMesh(mesh);

         // Element-domain locals are lowered to state cells, so without the same state
         // the node itself allocates, a write like a for loop's `j += 1` is dropped and
         // the loop never terminates.
         Field::FieldState state;
         for (const auto& ds : node.Program()->declaredStates)
            state.DeclareCell(ds.name, ds.typeName, ds.type, ds.lanes, ds.initialValues, ds.domain);
         state.Allocate(Field::Domain::Element, (size_t)N);

         Field::ElementVM vm;
         Field::ExecutionEnv env;
         env.t = 1.5;
         env.state = &state;
         std::string vmErr;
         node.Program()->ResetCounters();
         if (!vm.Execute(*node.Program(), store, env, vmErr))
         {
            outErr = vmErr.empty() ? std::string("VM refused the program") : vmErr;
            return false;
         }

         outPrologueRuns = node.Program()->prologueEvalCount;
         outValue = store.Py()[0];
         return true;
      };

      for (const auto& pair : kPairs)
      {
         double frameVal = 0.0, elemVal = 0.0;
         uint64_t framePrologueRuns = 0, elemPrologueRuns = 0;
         std::string err;

         if (!runCode(pair.frameCode, frameVal, framePrologueRuns, err))
         {
            printf("BothDomains: FAIL - '%s' frame form did not run: %s\n", pair.name, err.c_str());
            secOk = false;
            continue;
         }
         if (!runCode(pair.elemCode, elemVal, elemPrologueRuns, err))
         {
            printf("BothDomains: FAIL - '%s' element form did not run: %s\n", pair.name, err.c_str());
            secOk = false;
            continue;
         }

         // Without this the row is vacuous: if the expression is never hoisted, both
         // forms run in the element loop and agreeing proves nothing.
         if (framePrologueRuns != 1)
         {
            printf("BothDomains: FAIL - '%s' frame form was not hoisted (prologue ran %llu times)\n",
                   pair.name, (unsigned long long)framePrologueRuns);
            secOk = false;
         }

         if (std::fabs(frameVal - pair.expected) > 1e-5)
         {
            printf("BothDomains: FAIL - '%s' frame form gave %.4f, expected %.4f\n",
                   pair.name, frameVal, pair.expected);
            secOk = false;
         }
         if (std::fabs(elemVal - pair.expected) > 1e-5)
         {
            printf("BothDomains: FAIL - '%s' element form gave %.4f, expected %.4f\n",
                   pair.name, elemVal, pair.expected);
            secOk = false;
         }
         if (std::fabs(frameVal - elemVal) > 1e-5)
         {
            printf("BothDomains: FAIL - '%s' frame %.4f != element %.4f\n",
                   pair.name, frameVal, elemVal);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 9 (Same Expression Both Domains): OK\n");
      else { printf("SECTION 9 (Same Expression Both Domains): FAIL\n"); allOk = false; }
   }

   // 10. FieldPrimitiveNode conformance (pure generator, no input slot, presets compile,
   //     truncation cap, synthesized mesh generation).
   {
      bool secOk = true;
      FieldPrimitiveNode prim;

      if (prim.GeometryInputSlot(0) != nullptr)
      {
         printf("FieldPrimitive: FAIL - GeometryInputSlot must be nullptr for pure generator\n");
         secOk = false;
      }
      if (prim.PassthroughSource() != nullptr)
      {
         printf("FieldPrimitive: FAIL - PassthroughSource must be nullptr\n");
         secOk = false;
      }
      if (prim.OutputCount() < 1 || std::string(prim.OutputLabel(0)) != "geo")
      {
         printf("FieldPrimitive: FAIL - slot 0 output must be 'geo'\n");
         secOk = false;
      }

      // Check all 6 presets compile cleanly
      for (const auto& preset : FieldPrimitiveNode::Presets())
      {
         FieldPrimitiveNode node;
         node.code = preset.code;
         if (!node.Apply())
         {
            printf("FieldPrimitive: FAIL - preset '%s' did not compile: %s\n", preset.name, node.LastError().c_str());
            secOk = false;
         }
      }

      // Check synthesis and execution
      prim.count = 64;
      prim.maxElements = 1000;
      prim.CookIfNeeded(1);
      if (prim.GetMesh().vertices.size() != 64)
      {
         printf("FieldPrimitive: FAIL - expected 64 vertices, got %zu\n", prim.GetMesh().vertices.size());
         secOk = false;
      }
      if (prim.WasTruncated())
      {
         printf("FieldPrimitive: FAIL - count 64 <= max 1000 should not be truncated\n");
         secOk = false;
      }

      // Check truncation cap
      prim.count = 200;
      prim.maxElements = 50;
      prim.CookIfNeeded(2);
      if (prim.GetMesh().vertices.size() != 50 || !prim.WasTruncated())
      {
         printf("FieldPrimitive: FAIL - truncation failed (size=%zu, wasTruncated=%d)\n",
                prim.GetMesh().vertices.size(), prim.WasTruncated());
         secOk = false;
      }

      if (secOk) printf("SECTION 10 (FieldPrimitiveNode Pure Generation): OK\n");
      else { printf("SECTION 10 (FieldPrimitiveNode Pure Generation): FAIL\n"); allOk = false; }
   }

   // ---- Build step 23 (OPEN-B): element neighbour reads ----

   // Drives a program over a straight-line mesh for `cooks` cooks, sharing one
   // state bank and one store across them, and hands back the final positions.
   // `age` is advanced the way FieldElementNode advances it.
   auto runElementCooks = [](const std::string& code,
                             int n,
                             int cooks,
                             std::vector<float>& outX,
                             std::vector<float>& outY,
                             std::string& outErr) -> bool
   {
      Mesh mesh;
      for (int i = 0; i < n; ++i)
      {
         Vertex v;
         v.px = (float)i / (float)(n - 1);
         v.py = 0.0f;
         v.pz = 0.0f;
         mesh.vertices.push_back(v);
      }

      FieldElementNode node;
      node.code = code;
      if (!node.Apply()) { outErr = node.LastError(); return false; }

      Field::FieldState state;
      for (const auto& ds : node.Program()->declaredStates)
         state.DeclareCell(ds.name, ds.typeName, ds.type, ds.lanes, ds.initialValues, ds.domain);
      state.Allocate(Field::Domain::Element, (size_t)n);

      // Params default to their declared value, the way the node's ParamTable
      // seeds them. Leaving env.params null instead makes every param read 0,
      // which silently turns a simulation into a no-op.
      std::map<std::string, float> params;
      for (const auto& dp : node.Program()->declaredParams)
         params[dp.name] = (float)dp.defaultValue;

      Field::ElementStore store;
      Field::ElementVM vm;
      Mesh outMesh = mesh;

      for (int c = 0; c < cooks; ++c)
      {
         store.FromMesh(mesh);
         for (const auto& decl : node.Program()->declaredAttribs)
            store.DeclareAttrib(decl.first, decl.second);

         Field::ExecutionEnv env;
         env.t = 0.05 * (double)c;
         env.dt = 1.0 / 60.0;
         env.frame = 1000.0 + (double)c;
         env.age = (double)c;
         env.params = &params;
         env.state = &state;
         std::string err;
         if (!vm.Execute(*node.Program(), store, env, err)) { outErr = err; return false; }
         store.ToMesh(outMesh, node.Program()->WriteMask(), mesh);
      }

      outX.clear(); outY.clear();
      for (const auto& v : outMesh.vertices) { outX.push_back(v.px); outY.push_back(v.py); }
      return true;
   };

   // 11. A neighbour read actually reaches the neighbour.
   //
   // This is the load-bearing assertion of the whole step. A ramp mesh has
   // P.x = i/(n-1), so `P.at(i - 1).x` must be one step BELOW P.x everywhere
   // except element 0, where the clamp makes it equal. If the index expression
   // were ever dropped and `.at()` silently read the current element, every
   // delta would be 0 and this fails.
   {
      bool secOk = true;
      const int N = 16;
      std::vector<float> xs, ys;
      std::string err;
      if (!runElementCooks("nbr = P.at(i - 1)\nP.y = P.x - nbr.x\n", N, 1, xs, ys, err))
      {
         printf("Neighbour: FAIL - %s\n", err.c_str());
         secOk = false;
      }
      else
      {
         const float expect = 1.0f / (float)(N - 1);
         if (std::fabs(ys[0]) > 1e-5f)
         {
            printf("Neighbour: FAIL - element 0 should clamp to itself (delta %.6f, expected 0)\n", ys[0]);
            secOk = false;
         }
         for (int i = 1; i < N; ++i)
         {
            if (std::fabs(ys[i] - expect) > 1e-4f)
            {
               printf("Neighbour: FAIL - delta at i=%d is %.6f, expected %.6f\n", i, ys[i], expect);
               secOk = false;
               break;
            }
         }
      }
      if (secOk) printf("SECTION 11 (Neighbour Read Reaches The Neighbour): OK\n");
      else { printf("SECTION 11 (Neighbour Read Reaches The Neighbour): FAIL\n"); allOk = false; }
   }

   // 12. The read is order-independent: it sees the cook's INPUT, never a value
   // written earlier in this same loop.
   //
   // The kernel overwrites P.x with 99 and then reads its left neighbour. If
   // `.at()` read live lanes, elements 1.. would see 99; reading the snapshot
   // they must all still see the original ramp. This is the property that keeps
   // the element loop vectorizable, so it gets its own test.
   {
      bool secOk = true;
      const int N = 8;
      std::vector<float> xs, ys;
      std::string err;
      if (!runElementCooks("P.x = 99.0\nP.y = P.at(i - 1).x\n", N, 1, xs, ys, err))
      {
         printf("Order: FAIL - %s\n", err.c_str());
         secOk = false;
      }
      else
      {
         for (int i = 1; i < N; ++i)
         {
            const float expect = (float)(i - 1) / (float)(N - 1);
            if (std::fabs(ys[i] - expect) > 1e-4f)
            {
               printf("Order: FAIL - i=%d read %.4f (expected input ramp %.4f%s)\n",
                      i, ys[i], expect,
                      (std::fabs(ys[i] - 99.0f) < 1e-3f) ? " - it read the LIVE lane" : "");
               secOk = false;
               break;
            }
         }
      }
      if (secOk) printf("SECTION 12 (Neighbour Read Is Order-Independent): OK\n");
      else { printf("SECTION 12 (Neighbour Read Is Order-Independent): FAIL\n"); allOk = false; }
   }

   // 13. A neighbour read of an element STATE cell sees the PREVIOUS cook.
   //
   // The cell counts cooks per element; reading the left neighbour's counter
   // must therefore be one behind after each cook. If the snapshot were taken
   // after the loop instead of before it, this would read the current value.
   {
      bool secOk = true;
      const int N = 8;
      std::vector<float> xs, ys;
      std::string err;
      const char* code = "state float c = 0\nc += 1.0\nP.y = c - c.at(i - 1)\n";
      if (!runElementCooks(code, N, 4, xs, ys, err))
      {
         printf("StateNbr: FAIL - %s\n", err.c_str());
         secOk = false;
      }
      else if (std::fabs(ys[3] - 1.0f) > 1e-4f)
      {
         printf("StateNbr: FAIL - own counter minus previous-cook neighbour is %.4f, expected 1.0\n", ys[3]);
         secOk = false;
      }
      if (secOk) printf("SECTION 13 (State Neighbour Read Is One Cook Behind): OK\n");
      else { printf("SECTION 13 (State Neighbour Read Is One Cook Behind): FAIL\n"); allOk = false; }
   }

   // 14. Refusals: `.at()` on a value that has no per-element extent, and on an
   // undeclared name. Both messages must say why, not just "error".
   {
      bool secOk = true;

      FieldElementNode a;
      a.code = "param float k = 1.0 [0, 2]\nP.y = k.at(i - 1)\n";
      if (a.Apply())
      {
         printf("Refusal: FAIL - '.at()' on a graph-domain param was accepted\n");
         secOk = false;
      }
      else if (a.LastError().find("not one per element") == std::string::npos)
      {
         printf("Refusal: FAIL - param message does not explain the extent: %s\n", a.LastError().c_str());
         secOk = false;
      }

      FieldElementNode b;
      b.code = "P.y = wobble.at(i - 1)\n";
      if (b.Apply())
      {
         printf("Refusal: FAIL - '.at()' on an undeclared name was accepted\n");
         secOk = false;
      }
      else if (b.LastError().find("not declared") == std::string::npos)
      {
         printf("Refusal: FAIL - undeclared message is unclear: %s\n", b.LastError().c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 14 (Neighbour Read Refusals): OK\n");
      else { printf("SECTION 14 (Neighbour Read Refusals): FAIL\n"); allOk = false; }
   }

   // 15. Cross-lane builtins return their actual value.
   //
   // length/normalize/distance/dot/cross were missing from the element
   // interpreter and fell through its lane loop as 0.0, which is what made the
   // shipping "Radial Ripple" and "Spherical Bulge" presets uniform instead of
   // radial. A 3-4-5 triangle pins it: length(vec3(3,4,0)) is 5, not 0.
   {
      bool secOk = true;
      const int N = 4;
      std::vector<float> xs, ys;
      std::string err;
      if (!runElementCooks("P.y = length(vec3(3.0, 4.0, 0.0))\nP.x = dot(vec3(1.0, 2.0, 3.0), vec3(4.0, 5.0, 6.0))\n",
                           N, 1, xs, ys, err))
      {
         printf("CrossLane: FAIL - %s\n", err.c_str());
         secOk = false;
      }
      else
      {
         if (std::fabs(ys[0] - 5.0f) > 1e-4f)
         {
            printf("CrossLane: FAIL - length(3,4,0) = %.4f, expected 5\n", ys[0]);
            secOk = false;
         }
         if (std::fabs(xs[0] - 32.0f) > 1e-4f)
         {
            printf("CrossLane: FAIL - dot((1,2,3),(4,5,6)) = %.4f, expected 32\n", xs[0]);
            secOk = false;
         }
      }

      std::vector<float> nx, ny;
      if (secOk && runElementCooks("v = normalize(vec3(0.0, 3.0, 0.0))\nP.y = v.y\n", N, 1, nx, ny, err))
      {
         if (std::fabs(ny[0] - 1.0f) > 1e-4f)
         {
            printf("CrossLane: FAIL - normalize((0,3,0)).y = %.4f, expected 1\n", ny[0]);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 15 (Cross-Lane Builtins): OK\n");
      else { printf("SECTION 15 (Cross-Lane Builtins): FAIL\n"); allOk = false; }
   }

   // 16. `age` fires exactly once, and the two shipped simulations evolve.
   //
   // A seed gated on `age` must land one time only - a kernel that adds 0.25 on
   // its first cook and nothing after holds 0.25 after twelve cooks, not 3.0.
   // Then both new presets are run for 90 cooks and required to have MOVED and
   // to have stayed finite: a preset that compiles but sits still, or blows up,
   // is not a simulation.
   {
      bool secOk = true;
      const int N = 24;
      std::vector<float> xs, ys;
      std::string err;
      const char* seedCode =
         "state float acc = 0\n"
         "first = 1.0 - step(0.5, age)\n"
         "acc += 0.25 * first\n"
         "P.y = acc\n";
      if (!runElementCooks(seedCode, N, 12, xs, ys, err))
      {
         printf("Age: FAIL - %s\n", err.c_str());
         secOk = false;
      }
      else if (std::fabs(ys[0] - 0.25f) > 1e-4f)
      {
         printf("Age: FAIL - accumulator is %.4f after 12 cooks; 0.25 means once, 3.0 means every cook\n", ys[0]);
         secOk = false;
      }

      for (const auto& preset : FieldElementNode::Presets())
      {
         const std::string pn(preset.name);
         if (pn != "Verlet Rope" && pn != "Buckling Ribbon") continue;

         std::vector<float> px, py;
         std::string perr;
         if (!runElementCooks(preset.code, 40, 90, px, py, perr))
         {
            printf("Preset: FAIL - '%s': %s\n", preset.name, perr.c_str());
            secOk = false;
            continue;
         }

         double moved = 0.0;
         bool finite = true;
         for (size_t k = 0; k < px.size(); ++k)
         {
            if (!std::isfinite(px[k]) || !std::isfinite(py[k])) finite = false;
            moved += std::fabs((double)py[k]);
            moved += std::fabs((double)px[k] - (double)k / (double)(px.size() - 1));
         }
         if (!finite)
         {
            printf("Preset: FAIL - '%s' produced a non-finite position\n", preset.name);
            secOk = false;
         }
         if (moved < 1e-3)
         {
            printf("Preset: FAIL - '%s' never left its input mesh after 90 cooks (total motion %.6f)\n",
                   preset.name, moved);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 16 (age Seeds Once & Simulations Evolve): OK\n");
      else { printf("SECTION 16 (age Seeds Once & Simulations Evolve): FAIL\n"); allOk = false; }
   }

   // 17. Bugfix (reduce-local-variable-storage): reduce.<op>(x) over a plain
   // element-domain local (an ordinary assignment, not P/N/uv/Cd and not an
   // `attrib` declaration) must read this cook's REAL per-element data, not
   // silently read 0.0. Before the fix, `wave`/`hit` below had no persistent
   // per-element storage - OpReduceElementAttrib's GetAttribLane(name, lane)
   // always missed - and the value being domain-coarsened to Frame also
   // hoisted it into the prologue, which runs BEFORE the element loop
   // populates the buffer for this cook, so even fixing storage alone would
   // still read stale/zero data on the first cook.
   {
      bool secOk = true;

      auto cookOnce = [](const std::string& code,
                          const Mesh& mesh,
                          const char* outputName,
                          float& outValue,
                          std::string& outErr) -> bool
      {
         FieldElementNode node;
         node.code = code;
         if (!node.Apply()) { outErr = node.LastError(); return false; }

         Field::FieldState state;
         for (const auto& ds : node.Program()->declaredStates)
            state.DeclareCell(ds.name, ds.typeName, ds.type, ds.lanes, ds.initialValues, ds.domain);
         state.Allocate(Field::Domain::Element, mesh.vertices.size());

         std::map<std::string, float> params;
         for (const auto& dp : node.Program()->declaredParams)
            params[dp.name] = (float)dp.defaultValue;

         Field::ElementStore store;
         store.FromMesh(mesh);
         for (const auto& decl : node.Program()->declaredAttribs)
            store.DeclareAttrib(decl.first, decl.second);

         Field::ElementVM vm;
         Field::ExecutionEnv env;
         env.t = 0.0;
         env.dt = 1.0 / 60.0;
         env.params = &params;
         env.state = &state;
         std::string err;
         if (!vm.Execute(*node.Program(), store, env, err)) { outErr = err; return false; }

         if (!vm.ReadFrameVar(outputName, outValue))
         {
            outErr = std::string("output '") + outputName + "' was not readable via ReadFrameVar";
            return false;
         }
         return true;
      };

      // 17a. reduce.max over a plain local ('wave'), no `attrib` declaration.
      {
         const int N = 64;
         const float freq = 6.0f;
         Mesh mesh;
         for (int i = 0; i < N; ++i)
         {
            Vertex v;
            v.px = (float)i / (float)(N - 1); // P.x in [0, 1]
            v.py = 0.0f; v.pz = 0.0f;
            mesh.vertices.push_back(v);
         }

         double expectedMax = -1e30;
         for (int i = 0; i < N; ++i)
         {
            double x = (double)i / (double)(N - 1);
            double s = std::sin(x * (double)freq);
            if (s > expectedMax) expectedMax = s;
         }

         std::string code =
            "param float freq = 6.0 [1.0, 20.0]\n"
            "wave = sin(P.x * freq)\n"
            "output frame float wobble = reduce.max(wave)\n";

         float wobble = 0.0f;
         std::string err;
         if (!cookOnce(code, mesh, "wobble", wobble, err))
         {
            printf("Reduce-local: FAIL - reduce.max(wave) kernel: %s\n", err.c_str());
            secOk = false;
         }
         else if (std::fabs(wobble) < 1e-6f)
         {
            printf("Reduce-local: FAIL - reduce.max(wave) over a plain local read 0.0 (bug not fixed); expected ~%.4f\n", expectedMax);
            secOk = false;
         }
         else if (std::fabs((double)wobble - expectedMax) > 1e-3)
         {
            printf("Reduce-local: FAIL - reduce.max(wave) = %.6f, expected %.6f\n", wobble, expectedMax);
            secOk = false;
         }
      }

      // 17b. The shipped "Boundary Chime Sensor" preset: chime must be 0.0
      // when no vertex crosses threshold, and non-zero once one does.
      {
         const FieldElementNode::Preset* chimePreset = nullptr;
         for (const auto& preset : FieldElementNode::Presets())
         {
            if (std::string(preset.name) == "Boundary Chime Sensor") { chimePreset = &preset; break; }
         }

         if (!chimePreset)
         {
            printf("Reduce-local: FAIL - could not find 'Boundary Chime Sensor' preset\n");
            secOk = false;
         }
         else
         {
            Mesh flatMesh;
            for (int i = 0; i < 32; ++i)
            {
               Vertex v; v.px = (float)i / 31.0f; v.py = 0.0f; v.pz = 0.0f;
               flatMesh.vertices.push_back(v);
            }

            float chimeFlat = -1.0f;
            std::string ferr;
            if (!cookOnce(chimePreset->code, flatMesh, "chime", chimeFlat, ferr))
            {
               printf("Reduce-local: FAIL - Boundary Chime Sensor (flat): %s\n", ferr.c_str());
               secOk = false;
            }
            else if (chimeFlat != 0.0f)
            {
               printf("Reduce-local: FAIL - Boundary Chime Sensor read chime=%.4f with no vertex above threshold (expected 0.0)\n", chimeFlat);
               secOk = false;
            }

            Mesh hitMesh = flatMesh;
            hitMesh.vertices[0].py = 5.0f; // well above the default 0.3 threshold

            float chimeHit = -1.0f;
            std::string herr;
            if (!cookOnce(chimePreset->code, hitMesh, "chime", chimeHit, herr))
            {
               printf("Reduce-local: FAIL - Boundary Chime Sensor (hit): %s\n", herr.c_str());
               secOk = false;
            }
            else if (std::fabs(chimeHit - 1.0f) > 1e-4f)
            {
               printf("Reduce-local: FAIL - Boundary Chime Sensor read chime=%.4f with a vertex above threshold (expected 1.0)\n", chimeHit);
               secOk = false;
            }
         }
      }

      if (secOk) printf("SECTION 17 (reduce over a plain element-domain local reads real per-element data): OK\n");
      else { printf("SECTION 17 (reduce over a plain element-domain local reads real per-element data): FAIL\n"); allOk = false; }
   }

   printf("INFINITE_FIELDELEMENTTEST: %s\n", allOk ? "OK" : "FAIL");
   return allOk ? 0 : 1;
}

// ============================================================ INFINITE_FIELDPARAMTEST
int RunFieldParamTest()
{
   printf("[FIELDPARAMTEST] Running Field param declarations harness...\n");
   fflush(stdout);
   bool allOk = true;

   // Every Find() in the sections below used to be dereferenced blind. When a section's
   // Apply() failed the fixture took a null deref and the whole process died, so
   // sections 3 and later never ran at all - a segfault reports nothing, a FAIL line
   // reports which section broke and lets the rest of the harness finish.
   auto applyOk = [](FieldElementNode& n, const char* section) -> bool {
      if (n.Apply())
         return true;
      printf("%s: FAIL - Apply() failed: %s\n", section, n.LastError().c_str());
      return false;
   };
   auto paramId = [](FieldElementNode& n, const char* name, const char* section) -> int {
      const auto* p = n.GetParamTable().Find(name);
      if (p)
         return p->id;
      printf("%s: FAIL - param '%s' is not in the param table\n", section, name);
      return -1;
   };

   // SECTION 1: Parse, lowering & refusal diagnostics (§5.2, §5.7)
   {
      bool secOk = true;

      // 1. Valid param declarations
      {
         std::string code = "param float amount = 0.5 [0, 2]\nparam float a = -1.5 [-2.0, 5.0]\nP.y += amount + a\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         if (!Field::Lex(code, tokens, err))
         {
            printf("Parse: FAIL - valid snippet failed lexing: %s\n", err.message.c_str());
            secOk = false;
         }
         Field::AstNodePtr ast;
         if (!Field::ParseProgram(tokens, ast, err))
         {
            printf("Parse: FAIL - valid snippet failed parsing: %s\n", err.message.c_str());
            secOk = false;
         }
         Field::ElementIRProgram ir;
         if (!Field::LowerElementProgramToIR(ast, ir, err))
         {
            printf("Parse: FAIL - valid snippet failed lowering: %s\n", err.message.c_str());
            secOk = false;
         }
         if (ir.declaredParams.size() != 2 ||
             ir.declaredParams[0].name != "amount" || ir.declaredParams[0].defaultValue != 0.5 ||
             ir.declaredParams[0].minValue != 0.0 || ir.declaredParams[0].maxValue != 2.0 ||
             ir.declaredParams[1].name != "a" || ir.declaredParams[1].defaultValue != -1.5 ||
             ir.declaredParams[1].minValue != -2.0 || ir.declaredParams[1].maxValue != 5.0)
         {
            printf("Parse: FAIL - declared params values do not match expected\n");
            secOk = false;
         }
      }

      // 2. Refusal cases
      auto testRefusal = [&](const std::string& snippet, const std::string& expectedSubstr, const std::string& testName) {
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         if (!Field::Lex(snippet, tokens, err))
         {
            if (err.message.find(expectedSubstr) == std::string::npos)
            {
               printf("Refusal (%s): FAIL - error '%s' does not contain '%s'\n",
                      testName.c_str(), err.message.c_str(), expectedSubstr.c_str());
               secOk = false;
            }
            if (err.span.line <= 0 || err.span.col <= 0)
            {
               printf("Refusal (%s): FAIL - missing valid source span (line=%d, col=%d)\n",
                      testName.c_str(), err.span.line, err.span.col);
               secOk = false;
            }
            return;
         }
         Field::AstNodePtr ast;
         if (!Field::ParseProgram(tokens, ast, err))
         {
            if (err.message.find(expectedSubstr) == std::string::npos)
            {
               printf("Refusal (%s): FAIL - error '%s' does not contain '%s'\n",
                      testName.c_str(), err.message.c_str(), expectedSubstr.c_str());
               secOk = false;
            }
            if (err.span.line <= 0 || err.span.col <= 0)
            {
               printf("Refusal (%s): FAIL - missing valid source span (line=%d, col=%d)\n",
                      testName.c_str(), err.span.line, err.span.col);
               secOk = false;
            }
            return;
         }
         Field::ElementIRProgram ir;
         if (!Field::LowerElementProgramToIR(ast, ir, err))
         {
            if (err.message.find(expectedSubstr) == std::string::npos)
            {
               printf("Refusal (%s): FAIL - error '%s' does not contain '%s'\n",
                      testName.c_str(), err.message.c_str(), expectedSubstr.c_str());
               secOk = false;
            }
            if (err.span.line <= 0 || err.span.col <= 0)
            {
               printf("Refusal (%s): FAIL - missing valid source span (line=%d, col=%d)\n",
                      testName.c_str(), err.span.line, err.span.col);
               secOk = false;
            }
            return;
         }
         printf("Refusal (%s): FAIL - unexpectedly succeeded\n", testName.c_str());
         secOk = false;
      };

      testRefusal("param amount = 0.5 [0, 2]\n", "type is required", "Missing type");
      testRefusal("param float P = 0.5 [0, 1]\n", "'P' is a reserved word of the element domain", "Shadow element P");
      testRefusal("param float t = 0.5 [0, 1]\n", "'t' is a reserved word of the frame domain", "Shadow frame t");
      testRefusal("param float a = 0.5\n", "expected range '[min, max]'", "Missing range");
      testRefusal("param float a = 0.5 [2, 0]\n", "min must be <= max", "Inverted range");
      testRefusal("param vec3 c = 0.5 [0, 1]\n", "float params only in v1", "Non-float param");
      testRefusal("param float a = 0.5 [0, 1]\nparam float a = 1.0 [0, 2]\n", "duplicate declaration of param 'a'", "Duplicate param");
      testRefusal("param float a = 0.5 [0, 1 + 2]\n", "range bounds must be literal numbers", "Expression in range");
      testRefusal("param float a = 0.5 [0, 1]\na = 1.0\n", "cannot assign to read-only variable 'a'", "Assign to param");

      // 129 params ceiling (§5.7)
      std::string manyParams;
      for (int i = 0; i < 129; ++i)
      {
         manyParams += "param float p" + std::to_string(i) + " = 0 [0, 1]\n";
      }
      manyParams += "P.x += p0\n";
      testRefusal(manyParams, "kMaxParams", "128 ceiling (kMaxParams)");
      testRefusal(manyParams, "ParamMailbox.h", "128 ceiling (ParamMailbox.h)");

      if (secOk) printf("SECTION 1 (Parse & Refusals): OK\n");
      else { printf("SECTION 1 (Parse & Refusals): FAIL\n"); allOk = false; }
   }

   // SECTION 2: Registration & Collapsed State (§5.3)
   {
      bool secOk = true;
      FieldElementNode fe;
      fe.code = "param float amount = 0.75 [0, 2]\nparam float speed = 3.0 [0, 10]\nP.y += sin(P.x * speed + t) * amount\n";
      if (!fe.Apply())
      {
         printf("Registration: FAIL - Apply() failed: %s\n", fe.LastError().c_str());
         secOk = false;
      }

      Modulation::Instance().Clear();
      gParamRegisterOnly = true;
      BeginNodeParams(10);
      DrawFieldElementParams(&fe);
      EndNodeParams();
      gParamRegisterOnly = false;

      const auto* amountParam = fe.GetParamTable().Find("amount");
      const auto* speedParam = fe.GetParamTable().Find("speed");
      if (!amountParam || !speedParam)
      {
         printf("Registration: FAIL - params not found in param table\n");
         secOk = false;
      }
      else
      {
         // Declared params register under kFieldDeclaredParamBase + id, not
         // the raw ParamTable id - see DrawFieldParamSliders.
         const ParamRef* kAmount = Modulation::Instance().KnownParam(10, kFieldDeclaredParamBase + amountParam->id);
         const ParamRef* kSpeed = Modulation::Instance().KnownParam(10, kFieldDeclaredParamBase + speedParam->id);
         if (!kAmount || kAmount->name != "amount" || kAmount->minValue != 0.0f || kAmount->maxValue != 2.0f)
         {
            printf("Registration: FAIL - amount not registered correctly in Modulation\n");
            secOk = false;
         }
         if (!kSpeed || kSpeed->name != "speed" || kSpeed->minValue != 0.0f || kSpeed->maxValue != 10.0f)
         {
            printf("Registration: FAIL - speed not registered correctly in Modulation\n");
            secOk = false;
         }
      }

      // Collapsed registration (gParamRegisterOnly)
      Modulation::Instance().ClearFrameParams();
      gParamRegisterOnly = true;
      BeginNodeParams(10);
      DrawFieldElementParams(&fe);
      EndNodeParams();
      gParamRegisterOnly = false;

      const auto& frameParams = Modulation::Instance().FrameParams();
      bool foundAmount = false, foundSpeed = false;
      for (const auto& r : frameParams)
      {
         if (r.nodeIndex == 10 && r.name == "amount") foundAmount = true;
         if (r.nodeIndex == 10 && r.name == "speed") foundSpeed = true;
      }
      if (!foundAmount || !foundSpeed)
      {
         printf("Registration: FAIL - collapsed node (gParamRegisterOnly) did not register params\n");
         secOk = false;
      }

      if (secOk) printf("SECTION 2 (Registration & Collapsed): OK\n");
      else { printf("SECTION 2 (Registration & Collapsed): FAIL\n"); allOk = false; }
   }

   // SECTION 3: Binding Survives Insert (§5.5)
   {
      bool secOk = true;

      do
      {
         FieldElementNode fe;
         fe.code = "param float amount = 0.75 [0, 2]\nparam float speed = 3.0 [0, 10]\nP.y += sin(P.x * speed + t) * amount\n";
         if (!applyOk(fe, "SurvivesInsert")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         int speedId = paramId(fe, "speed", "SurvivesInsert");
         if (speedId < 0) { secOk = false; break; }
         Modulation::Instance().Bind(10, speedId, 99, 0);

         if (!Modulation::Instance().IsModulated(10, speedId))
         {
            printf("SurvivesInsert: FAIL - binding failed\n");
            secOk = false;
         }

         // Insert new param 'a' above 'amount' and 'speed'
         fe.code = "param float a = 0.1 [0, 1]\nparam float amount = 0.75 [0, 2]\nparam float speed = 3.0 [0, 10]\nP.y += sin(P.x * speed + t) * amount + a\n";
         if (!applyOk(fe, "SurvivesInsert")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         int newSpeedId = paramId(fe, "speed", "SurvivesInsert");
         int aId = paramId(fe, "a", "SurvivesInsert");
         if (newSpeedId < 0 || aId < 0) { secOk = false; break; }

         if (newSpeedId != speedId)
         {
            printf("SurvivesInsert: FAIL - speed ID shifted after insertion (%d -> %d)\n", speedId, newSpeedId);
            secOk = false;
         }
         if (!Modulation::Instance().IsModulated(10, speedId))
         {
            printf("SurvivesInsert: FAIL - modulation binding on speed was lost after insert\n");
            secOk = false;
         }
         if (Modulation::Instance().ModulatorFor(10, speedId).nodeIndex != 99)
         {
            printf("SurvivesInsert: FAIL - modulation binding pointed at wrong modulator\n");
            secOk = false;
         }
         if (Modulation::Instance().IsModulated(10, aId))
         {
            printf("SurvivesInsert: FAIL - newly inserted param 'a' was falsely modulated\n");
            secOk = false;
         }

      } while (false);

      if (secOk) printf("SECTION 3 (Binding Survives Insert): OK\n");
      else { printf("SECTION 3 (Binding Survives Insert): FAIL\n"); allOk = false; }
   }

   // SECTION 4: Binding Drops on Delete & Rename (§5.5)
   {
      bool secOk = true;

      do
      {
         FieldElementNode fe;
         fe.code = "param float a = 0.1 [0, 1]\nparam float amount = 0.75 [0, 2]\nparam float speed = 3.0 [0, 10]\nP.y += sin(P.x * speed + t) * amount + a\n";
         if (!applyOk(fe, "Delete/Rename")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         int speedId = paramId(fe, "speed", "Delete/Rename");
         int amountId = paramId(fe, "amount", "Delete/Rename");
         if (speedId < 0 || amountId < 0) { secOk = false; break; }
         Modulation::Instance().Bind(10, speedId, 99, 0);
         Modulation::Instance().Bind(10, amountId, 98, 0);

         // Delete 'speed'
         fe.code = "param float a = 0.1 [0, 1]\nparam float amount = 0.75 [0, 2]\nP.y += amount + a\n";
         if (!applyOk(fe, "Delete/Rename")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         if (Modulation::Instance().IsModulated(10, speedId))
         {
            printf("Delete/Rename: FAIL - deleted param 'speed' is still modulated\n");
            secOk = false;
         }
         if (Modulation::Instance().Links().find(Modulation::Key(10, speedId)) != Modulation::Instance().Links().end())
         {
            printf("Delete/Rename: FAIL - deleted param 'speed' left stale Key in Links()\n");
            secOk = false;
         }
         if (!fe.Notice().empty() && fe.Notice().find("speed") == std::string::npos)
         {
            printf("Delete/Rename: FAIL - notice does not mention deleted param name\n");
            secOk = false;
         }

         // Rename 'amount' to 'amount2'
         fe.code = "param float a = 0.1 [0, 1]\nparam float amount2 = 0.75 [0, 2]\nP.y += amount2 + a\n";
         if (!applyOk(fe, "Delete/Rename")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         if (Modulation::Instance().IsModulated(10, amountId))
         {
            printf("Delete/Rename: FAIL - renamed param 'amount' still has modulation on old ID\n");
            secOk = false;
         }
         if (fe.Notice().empty() || fe.Notice().find("amount") == std::string::npos)
         {
            printf("Delete/Rename: FAIL - renaming param did not surface notice naming param (notice: '%s')\n", fe.Notice().c_str());
            secOk = false;
         }

      } while (false);

      if (secOk) printf("SECTION 4 (Binding Drops on Delete & Rename): OK\n");
      else { printf("SECTION 4 (Binding Drops on Delete & Rename): FAIL\n"); allOk = false; }
   }

   // SECTION 5: Failed Compile Keeps Program & Bindings (§5.5)
   {
      bool secOk = true;

      do
      {
         FieldElementNode fe;
         fe.code = "param float a = 0.1 [0, 1]\nparam float amount = 0.75 [0, 2]\nP.y += amount + a\n";
         if (!applyOk(fe, "FailedCompile")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         int aId = paramId(fe, "a", "FailedCompile");
         int amountId = paramId(fe, "amount", "FailedCompile");
         if (aId < 0 || amountId < 0) { secOk = false; break; }
         fe.GetParamTable().Find("a")->value = 0.42f;
         Modulation::Instance().Bind(10, amountId, 99, 0);

         // Introduce broken syntax
         fe.code = "param float a = 0.1 [0, 1]\nthis is completely broken code syntax!!!\n";
         bool applyResult = fe.Apply();
         if (applyResult)
         {
            printf("FailedCompile: FAIL - broken syntax unexpectedly returned true from Apply()\n");
            secOk = false;
         }
         if (fe.LastError().empty())
         {
            printf("FailedCompile: FAIL - LastError() was empty on failed compile\n");
            secOk = false;
         }
         if (!fe.GetParamTable().Find("a") || fe.GetParamTable().Find("a")->value != 0.42f)
         {
            printf("FailedCompile: FAIL - param value was lost or reset on failed compile\n");
            secOk = false;
         }
         if (!fe.GetParamTable().Find("amount"))
         {
            printf("FailedCompile: FAIL - running param table was wiped on failed compile\n");
            secOk = false;
         }
         if (!Modulation::Instance().IsModulated(10, amountId))
         {
            printf("FailedCompile: FAIL - modulation binding was dropped on failed compile\n");
            secOk = false;
         }

      } while (false);

      if (secOk) printf("SECTION 5 (Failed Compile Preserves State): OK\n");
      else { printf("SECTION 5 (Failed Compile Preserves State): FAIL\n"); allOk = false; }
   }

   // SECTION 6: Save / Load Round-Trip & Undo (§5.4, §5.5)
   {
      bool secOk = true;

      do
      {
         FieldElementNode fe;
         fe.code = "param float freq = 2.5 [0, 10]\nparam float gain = 0.8 [0, 1]\nP.y += sin(P.x * freq) * gain\n";
         if (!applyOk(fe, "SaveLoad")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         int freqId = paramId(fe, "freq", "SaveLoad");
         int gainId = paramId(fe, "gain", "SaveLoad");
         if (freqId < 0 || gainId < 0) { secOk = false; break; }
         fe.GetParamTable().Find("freq")->value = 4.25f;
         fe.GetParamTable().Find("gain")->value = 0.33f;
         Modulation::Instance().Bind(10, gainId, 99, 0);

         std::vector<std::pair<std::string, std::string>> saved;
         Patch::SaveParams(&fe, saved);

         FieldElementNode fe2;
         Patch::LoadParams(&fe2, saved);
         if (!applyOk(fe2, "SaveLoad")) { secOk = false; break; }

         gParamRegisterOnly = true;
         BeginNodeParams(20);
         DrawFieldElementParams(&fe2);
         EndNodeParams();
         gParamRegisterOnly = false;

         if (fe2.code != fe.code)
         {
            printf("SaveLoad: FAIL - code did not round-trip\n");
            secOk = false;
         }
         const auto* f2Freq = fe2.GetParamTable().Find("freq");
         const auto* f2Gain = fe2.GetParamTable().Find("gain");
         if (!f2Freq || f2Freq->value != 4.25f)
         {
            printf("SaveLoad: FAIL - freq value did not round-trip (got %f, expected 4.25)\n", f2Freq ? f2Freq->value : -1.0f);
            secOk = false;
         }
         if (!f2Gain || f2Gain->value != 0.33f)
         {
            printf("SaveLoad: FAIL - gain value did not round-trip (got %f, expected 0.33)\n", f2Gain ? f2Gain->value : -1.0f);
            secOk = false;
         }
         if (!f2Gain || f2Gain->id != gainId)
         {
            printf("SaveLoad: FAIL - gain ID did not round-trip (%d vs %d)\n", f2Gain ? f2Gain->id : -1, gainId);
            secOk = false;
         }

         // Undo test: modify fe, then restore saved snapshot
         fe.code = "param float depth = 5.0 [0, 10]\nP.y += depth\n";
         if (!applyOk(fe, "Undo")) { secOk = false; break; }
         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         // Undo -> replay saved snapshot (both node params and modulation state)
         Patch::LoadParams(&fe, saved);
         if (!applyOk(fe, "Undo")) { secOk = false; break; }
         Modulation::Instance().Bind(10, gainId, 99, 0); // Undo restores patch-level modulation bindings

         gParamRegisterOnly = true;
         BeginNodeParams(10);
         DrawFieldElementParams(&fe);
         EndNodeParams();
         gParamRegisterOnly = false;

         const auto* undoneFreq = fe.GetParamTable().Find("freq");
         const auto* undoneGain = fe.GetParamTable().Find("gain");
         if (!undoneFreq || undoneFreq->value != 4.25f || !undoneGain || undoneGain->value != 0.33f || undoneGain->id != gainId)
         {
            printf("Undo: FAIL - undo failed to restore param values and stable IDs\n");
            secOk = false;
         }
         if (!undoneGain || !Modulation::Instance().IsModulated(10, undoneGain->id))
         {
            printf("Undo: FAIL - modulation binding was not attached to restored param ID\n");
            secOk = false;
         }

      } while (false);

      if (secOk) printf("SECTION 6 (Save/Load & Undo): OK\n");
      else { printf("SECTION 6 (Save/Load & Undo): FAIL\n"); allOk = false; }
      fflush(stdout);
   }

   printf("INFINITE_FIELDPARAMTEST: %s\n", allOk ? "OK" : "FAIL");
   fflush(stdout);
   return allOk ? 0 : 1;
}
}
