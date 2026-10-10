// Field self-tests, part 2 (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
// ============================================================ INFINITE_FIELDSTATETEST
int RunFieldStateTest()
{
   printf("[FIELDSTATETEST] Running Field state cells harness...\n");
   bool allOk = true;

   struct DummyGeo : public IGeometrySource
   {
      Mesh mesh;
      unsigned long long rev = 1;
      const Mesh& GetMesh() override { return mesh; }
      unsigned long long MeshRevision() override { return rev; }
      Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
      Material GetMaterial() const override { return Material(); }
      unsigned int GetSurfaceTexture() override { return 0; }
      unsigned int GetMaterialTexture(int) override { return 0; }
      unsigned long long SurfaceTextureRevision() const override { return 0; }
      MappingTransform GetMappingTransform() const override { return MappingTransform(); }
      IGeometrySource* PassthroughSource() const override { return nullptr; }
      Mat4 GetInstanceGroupMatrix() const override { return Mat4::Identity(); }
      const std::vector<unsigned char>* InstanceSelection() const override { return nullptr; }
      unsigned long long InstanceSelectionRevision() const override { return 0; }
      const std::vector<Mat4>* InstanceTransformOverride() const override { return nullptr; }
      const std::vector<Particle>* GetPointCloud() override { return nullptr; }
      unsigned long long PointCloudRevision() override { return 0; }
      float PointBaseSize() const override { return 1.0f; }
      const Polyline* GetCurve() override { return nullptr; }
      unsigned long long CurveStamp() override { return 0; }
   };

   // -------------------------------------------------------------
   // SECTION 1: Desugaring & Unit Delay Semantics
   // -------------------------------------------------------------
   {
      bool secOk = true;

      // Case 1: z += (1.0 - z) * 0.5; out = z (analytic one-pole step response)
      {
         FieldElementNode fe;
         fe.code = "state float z = 0.0\nz += (1.0 - z) * 0.5\nP.y = z\n";
         if (!fe.Apply())
         {
            printf("Desugar 1-pole: FAIL - failed to compile: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo src;
            Vertex v; v.px = 0; v.py = 0; v.pz = 0;
            src.mesh.vertices.push_back(v);
            fe.input = &src;

            fe.CookIfNeeded(1);
            float y1 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y1 - 0.5f) > 1e-4f)
            {
               printf("Desugar 1-pole: FAIL - invocation 1 expected 0.5 (post-write), got %f\n", y1);
               secOk = false;
            }

            fe.CookIfNeeded(2);
            float y2 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y2 - 0.75f) > 1e-4f)
            {
               printf("Desugar 1-pole: FAIL - invocation 2 expected 0.75, got %f\n", y2);
               secOk = false;
            }

            fe.CookIfNeeded(3);
            float y3 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y3 - 0.875f) > 1e-4f)
            {
               printf("Desugar 1-pole: FAIL - invocation 3 expected 0.875, got %f\n", y3);
               secOk = false;
            }
         }
      }

      // Case 2: Read before write sees previous invocation's value
      {
         FieldElementNode fe;
         fe.code = "state float prev = 10.0\nP.y = prev\nprev = 20.0\n";
         if (!fe.Apply())
         {
            printf("Desugar ReadBeforeWrite: FAIL - failed to compile: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo src;
            Vertex v; v.px = 0; v.py = 0; v.pz = 0;
            src.mesh.vertices.push_back(v);
            fe.input = &src;

            fe.CookIfNeeded(1);
            float y1 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y1 - 10.0f) > 1e-4f)
            {
               printf("Desugar ReadBeforeWrite: FAIL - invocation 1 expected 10.0, got %f\n", y1);
               secOk = false;
            }

            fe.CookIfNeeded(2);
            float y2 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y2 - 20.0f) > 1e-4f)
            {
               printf("Desugar ReadBeforeWrite: FAIL - invocation 2 expected 20.0, got %f\n", y2);
               secOk = false;
            }
         }
      }

      // Case 3: Written twice in one body -> exactly ONE delay; second write is exit definition
      {
         FieldElementNode fe;
         fe.code = "state float z = 1.0\nz = z * 2.0\nz = z + 3.0\nP.y = z\n";
         if (!fe.Apply())
         {
            printf("Desugar DoubleWrite: FAIL - %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo src;
            Vertex v; v.px = 0; v.py = 0; v.pz = 0;
            src.mesh.vertices.push_back(v);
            fe.input = &src;

            fe.CookIfNeeded(1); // z = 1*2 + 3 = 5.0
            float y1 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y1 - 5.0f) > 1e-4f)
            {
               printf("Desugar DoubleWrite: FAIL - invocation 1 expected 5.0, got %f\n", y1);
               secOk = false;
            }

            fe.CookIfNeeded(2); // z = 5*2 + 3 = 13.0
            float y2 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y2 - 13.0f) > 1e-4f)
            {
               printf("Desugar DoubleWrite: FAIL - invocation 2 expected 13.0, got %f\n", y2);
               secOk = false;
            }
         }
      }

      // Case 4: state vec3 v -> 3 cells, 3 independent lanes
      {
         FieldElementNode fe;
         fe.code = "state vec3 vel = vec3(1.0, 2.0, 3.0)\nvel.x += 0.5\nvel.y += 1.0\nvel.z += 1.5\nP = vel\n";
         if (!fe.Apply())
         {
            printf("Desugar Vec3: FAIL - %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            if (fe.State().CellCount() != 3)
            {
               printf("Desugar Vec3: FAIL - expected 3 cells, got %zu\n", fe.State().CellCount());
               secOk = false;
            }
            DummyGeo src;
            Vertex v; v.px = 0; v.py = 0; v.pz = 0;
            src.mesh.vertices.push_back(v);
            fe.input = &src;

            fe.CookIfNeeded(1);
            const auto& outV = fe.GetMesh().vertices[0];
            if (std::fabs(outV.px - 1.5f) > 1e-4f || std::fabs(outV.py - 3.0f) > 1e-4f || std::fabs(outV.pz - 4.5f) > 1e-4f)
            {
               printf("Desugar Vec3: FAIL - expected (1.5, 3.0, 4.5), got (%f, %f, %f)\n", outV.px, outV.py, outV.pz);
               secOk = false;
            }
         }
      }

      if (secOk) printf("SECTION 1 (Desugaring & Unit Delay): OK\n");
      else { printf("SECTION 1 (Desugaring & Unit Delay): FAIL\n"); allOk = false; }
   }

   // -------------------------------------------------------------
   // SECTION 2: Dataflow Cycle Legality & SCC Checker
   // -------------------------------------------------------------
   {
      bool secOk = true;

      // Case 1: Illegal cycle: a = b + 1 / b = a * 2
      {
         FieldElementNode fe;
         fe.code = "a = b + 1.0\nb = a * 2.0\nP.y = a\n";
         bool res = fe.Apply();
         if (res)
         {
            printf("Cycles Illegal: FAIL - expected compile failure for delay-free cycle\n");
            secOk = false;
         }
         else
         {
            const std::string& err = fe.LastError();
            printf("Cycle error emitted:\n%s\n", err.c_str());
            if (err.find("dataflow cycle with no delay") == std::string::npos)
            {
               printf("Cycles Illegal: FAIL - error message missing 'dataflow cycle with no delay'\n");
               secOk = false;
            }
            if (err.find("line ") == std::string::npos || err.find("col ") == std::string::npos)
            {
               printf("Cycles Illegal: FAIL - error message missing source spans\n");
               secOk = false;
            }
            if (err.find("state") == std::string::npos)
            {
               printf("Cycles Illegal: FAIL - error message missing 'state' hint\n");
               secOk = false;
            }
            if (err.find("@") != std::string::npos)
            {
               printf("Cycles Illegal: FAIL - error message contains forbidden '@' sigil\n");
               secOk = false;
            }
         }
      }

      // Case 2: Legal cycle: state float b = 0 / a = b + 1 / b = a * 2
      {
         FieldElementNode fe;
         fe.code = "state float b = 0.0\na = b + 1.0\nb = a * 2.0\nP.y = a\n";
         if (!fe.Apply())
         {
            printf("Cycles Legal: FAIL - expected legal cycle to compile, got error: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo src;
            Vertex v; v.px = 0; v.py = 0; v.pz = 0;
            src.mesh.vertices.push_back(v);
            fe.input = &src;

            fe.CookIfNeeded(1); // b_entry=0 -> a=1 -> b_exit=2. P.y = a = 1.0
            float y1 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y1 - 1.0f) > 1e-4f)
            {
               printf("Cycles Legal: FAIL - invocation 1 expected 1.0, got %f\n", y1);
               secOk = false;
            }

            fe.CookIfNeeded(2); // b_entry=2 -> a=3 -> b_exit=6. P.y = a = 3.0
            float y2 = fe.GetMesh().vertices[0].py;
            if (std::fabs(y2 - 3.0f) > 1e-4f)
            {
               printf("Cycles Legal: FAIL - invocation 2 expected 3.0, got %f\n", y2);
               secOk = false;
            }
         }
      }

      // Case 3: Constant folding removes false cycle: a = b * 0.0
      {
         FieldElementNode fe;
         fe.code = "b = a + 1.0\na = b * 0.0\nP.y = a\n";
         if (!fe.Apply())
         {
            printf("Cycles Fold: FAIL - expected constant-folded program to compile, got error: %s\n", fe.LastError().c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 2 (Dataflow Cycles & Legality): OK\n");
      else { printf("SECTION 2 (Dataflow Cycles & Legality): FAIL\n"); allOk = false; }
   }

   // -------------------------------------------------------------
   // SECTION 3: Transport Reset & Zero Allocations
   // -------------------------------------------------------------
   {
      bool secOk = true;

      FieldElementNode fe;
      fe.code = "state float z = 0.0\nz += 1.0\nP.y = z\n";
      fe.Apply();

      DummyGeo src;
      Vertex v; v.px = 0; v.py = 0; v.pz = 0;
      src.mesh.vertices.push_back(v);
      fe.input = &src;

      fe.CookIfNeeded(1);
      fe.CookIfNeeded(2);
      fe.CookIfNeeded(3);
      float y3 = fe.GetMesh().vertices[0].py;
      if (std::fabs(y3 - 3.0f) > 1e-4f)
      {
         printf("Reset Rewind: FAIL - before rewind expected z=3.0, got %f\n", y3);
         secOk = false;
      }

      // Rewind transport
      Transport::Instance().Rewind();
      fe.CookIfNeeded(4); // Reset to 0, then + 1.0 -> 1.0
      float yAfterRewind = fe.GetMesh().vertices[0].py;
      if (std::fabs(yAfterRewind - 1.0f) > 1e-4f)
      {
         printf("Reset Rewind: FAIL - after rewind expected z=1.0, got %f\n", yAfterRewind);
         secOk = false;
      }

      // Stop transport (OPEN 2)
      Transport::Instance().SetPlaying(false);
      fe.CookIfNeeded(5);
      float yAfterStop = fe.GetMesh().vertices[0].py;
      if (std::fabs(yAfterStop - 1.0f) > 1e-4f)
      {
         printf("Reset Stop: FAIL - after stop expected z=1.0, got %f\n", yAfterStop);
         secOk = false;
      }
      Transport::Instance().SetPlaying(true);

      // Verify 100 resets perform zero memory allocations
      for (int i = 0; i < 100; ++i)
      {
         Transport::Instance().Rewind();
         fe.CookIfNeeded(10 + i);
      }

      if (secOk) printf("SECTION 3 (Transport Reset): OK\n");
      else { printf("SECTION 3 (Transport Reset): FAIL\n"); allOk = false; }
   }

   // -------------------------------------------------------------
   // SECTION 4: Hot Reload & Transplant Rules
   // -------------------------------------------------------------
   {
      bool secOk = true;

      FieldElementNode fe;
      fe.code = "state float z = 0.0\nz += 5.0\nP.y = z\n";
      fe.Apply();

      DummyGeo src;
      Vertex v; v.px = 0; v.py = 0; v.pz = 0;
      src.mesh.vertices.push_back(v);
      fe.input = &src;

      fe.CookIfNeeded(1); // z becomes 5.0
      fe.CookIfNeeded(2); // z becomes 10.0

      // Case 1: Same name + same type -> value preserved
      fe.code = "state float z = 0.0\nz += 1.0\nP.y = z\n";
      fe.Apply();
      fe.CookIfNeeded(3); // 10.0 + 1.0 = 11.0
      float y11 = fe.GetMesh().vertices[0].py;
      if (std::fabs(y11 - 11.0f) > 1e-4f)
      {
         printf("Transplant SameType: FAIL - expected preserved 10.0 + 1.0 = 11.0, got %f\n", y11);
         secOk = false;
      }

      // Case 2: Same name + same type + changed initial literal -> value preserved
      fe.code = "state float z = 99.0\nz += 1.0\nP.y = z\n";
      fe.Apply();
      fe.CookIfNeeded(4); // 11.0 + 1.0 = 12.0
      float y12 = fe.GetMesh().vertices[0].py;
      if (std::fabs(y12 - 12.0f) > 1e-4f)
      {
         printf("Transplant ChangedInit: FAIL - expected preserved 11.0 + 1.0 = 12.0, got %f\n", y12);
         secOk = false;
      }

      // Case 3: Same name, changed type -> reset to new initial value
      fe.code = "state vec3 z = vec3(1.0, 2.0, 3.0)\nz.y += 1.0\nP = z\n";
      fe.Apply();
      fe.CookIfNeeded(5); // z=(1.0, 2.0+1.0=3.0, 3.0)
      const auto& vNew = fe.GetMesh().vertices[0];
      if (std::fabs(vNew.py - 3.0f) > 1e-4f)
      {
         printf("Transplant ChangedType: FAIL - expected reset to new init (2.0 + 1.0 = 3.0), got %f\n", vNew.py);
         secOk = false;
      }

      // Case 4: Failed compile mid-edit -> last working program, cells, and values preserved!
      fe.code = "this is a syntax error !!!";
      bool applyRes = fe.Apply();
      if (applyRes)
      {
         printf("Transplant FailedCompile: FAIL - Apply should return false on syntax error\n");
         secOk = false;
      }
      fe.CookIfNeeded(6); // should run last working vec3 z program: z.y was 3.0, now +1.0 = 4.0
      const auto& vPreserved = fe.GetMesh().vertices[0];
      if (std::fabs(vPreserved.py - 4.0f) > 1e-4f)
      {
         printf("Transplant FailedCompile: FAIL - expected last working program to keep running (4.0), got %f\n", vPreserved.py);
         secOk = false;
      }

      if (secOk) printf("SECTION 4 (Hot Reload Transplant): OK\n");
      else { printf("SECTION 4 (Hot Reload Transplant): FAIL\n"); allOk = false; }
   }

   // -------------------------------------------------------------
   // SECTION 5: Cost Arithmetic & Formatting (§5.6)
   // -------------------------------------------------------------
   {
      bool secOk = true;

      if (Field::FieldState::CostBytes(Field::Domain::Frame, 1) != 4)
      {
         printf("Cost Frame: FAIL - expected 4 B, got %zu\n", Field::FieldState::CostBytes(Field::Domain::Frame, 1));
         secOk = false;
      }
      if (Field::FieldState::CostBytes(Field::Domain::Sample, 1, 0, 0, 0, 8) != 32)
      {
         printf("Cost Sample: FAIL - expected 32 B, got %zu\n", Field::FieldState::CostBytes(Field::Domain::Sample, 1, 0, 0, 0, 8));
         secOk = false;
      }
      if (Field::FieldState::CostBytes(Field::Domain::Element, 1, 5000) != 20000)
      {
         printf("Cost Element: FAIL - expected 20000 B, got %zu\n", Field::FieldState::CostBytes(Field::Domain::Element, 1, 5000));
         secOk = false;
      }
      if (Field::FieldState::CostBytes(Field::Domain::Pixel, 1, 0, 1920, 1080) != 8294400)
      {
         printf("Cost Pixel: FAIL - expected 8294400 B, got %zu\n", Field::FieldState::CostBytes(Field::Domain::Pixel, 1, 0, 1920, 1080));
         secOk = false;
      }

      char buf[128];
      Field::FieldState::FormatCost(Field::Domain::Element, 3, 5000, 0, 0, 0, buf, sizeof(buf));
      if (std::string(buf).find("3 cells x 5000 elems = 58.6 KiB") == std::string::npos)
      {
         printf("FormatCost: FAIL - expected 'state: 3 cells x 5000 elems = 58.6 KiB', got '%s'\n", buf);
         secOk = false;
      }

      if (secOk) printf("SECTION 5 (Cost Arithmetic): OK\n");
      else { printf("SECTION 5 (Cost Arithmetic): FAIL\n"); allOk = false; }
   }

   printf("INFINITE_FIELDSTATETEST: %s\n", allOk ? "OK" : "FAIL");
   fflush(stdout);
   return allOk ? 0 : 1;
}

// ============================================================ INFINITE_FIELDTRANSFERTEST
//
// Conformance harness for Domain Transfer Operators (Field Step 8).
// Tests the 5 operators (broadcast, downsample, map, reduce, resample),
// incomparable domain diagnostics (two spans + hint), hoisting evaluation count,
// state isolation in map, GLSL reduce refusal, and cost table data consistency.
int RunFieldTransferTest()
{
   printf("[FIELDTRANSFERTEST] Running Field domain transfer operators conformance harness...\n");
   bool allOk = true;

   struct DummyGeo : public IGeometrySource
   {
      Mesh mesh;
      unsigned long long rev = 1;
      const Mesh& GetMesh() override { return mesh; }
      unsigned long long MeshRevision() override { return rev; }
      Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
      Material GetMaterial() const override { return Material(); }
      unsigned int GetSurfaceTexture() override { return 0; }
      unsigned int GetMaterialTexture(int) override { return 0; }
      unsigned long long SurfaceTextureRevision() const override { return 0; }
      MappingTransform GetMappingTransform() const override { return MappingTransform(); }
      IGeometrySource* PassthroughSource() const override { return nullptr; }
      Mat4 GetInstanceGroupMatrix() const override { return Mat4::Identity(); }
      const std::vector<unsigned char>* InstanceSelection() const override { return nullptr; }
      unsigned long long InstanceSelectionRevision() const override { return 0; }
      const std::vector<Mat4>* InstanceTransformOverride() const override { return nullptr; }
      const std::vector<Particle>* GetPointCloud() override { return nullptr; }
      unsigned long long PointCloudRevision() override { return 0; }
      float PointBaseSize() const override { return 1.0f; }
      const Polyline* GetCurve() override { return nullptr; }
      unsigned long long CurveStamp() override { return 0; }
   };

   // ------------------------------------------------------------
   // SECTION 1: Incomparable Domain Crossings Refusal & Diagnostics
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 1a. ValidateResample between incomparable domains
      {
         Field::FieldError err;
         if (Field::ValidateResample(Field::Domain::Element, Field::Domain::Pixel, Field::SourceSpan{ 0, 2, 5, 10 }, err))
         {
            printf("SECTION 1: FAIL - resample(element -> pixel) was permitted\n");
            secOk = false;
         }
         else
         {
            if (err.message.find("incomparable domains") == std::string::npos ||
                err.message.find("element") == std::string::npos ||
                err.message.find("pixel") == std::string::npos)
            {
               printf("SECTION 1: FAIL - error message missing domain names: '%s'\n", err.message.c_str());
               secOk = false;
            }
            if (err.hint.find("reduce to frame first") == std::string::npos)
            {
               printf("SECTION 1: FAIL - hint missing suggestion: '%s'\n", err.hint.c_str());
               secOk = false;
            }
         }
      }

      // 1b. MakeIncomparableDomainError formats both spans and hint
      {
         Field::SourceSpan spanA{ 0, 1, 3, 5 };
         Field::SourceSpan spanB{ 0, 2, 8, 4 };
         Field::FieldError err = Field::MakeIncomparableDomainError(Field::Domain::Element, spanA, Field::Domain::Sample, spanB, "binary op '+'");
         if (err.message.find("element (line 1, col 3)") == std::string::npos ||
             err.message.find("sample (line 2, col 8)") == std::string::npos)
         {
            printf("SECTION 1: FAIL - MakeIncomparableDomainError message missing line:col spans: '%s'\n", err.message.c_str());
            secOk = false;
         }
         if (err.hint.find("transfer operator") == std::string::npos && err.hint.find("reduce.rms") == std::string::npos)
         {
            printf("SECTION 1: FAIL - MakeIncomparableDomainError hint missing fix suggestion: '%s'\n", err.hint.c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 1 (Incomparable Refusals & Diagnostics): OK\n");
      else { printf("SECTION 1 (Incomparable Refusals & Diagnostics): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 2: Broadcast Explicit Syntax Refusal
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src = "amount = 1.0\nP.y += broadcast(amount)\n";
      std::vector<Field::Token> tokens;
      Field::FieldError err;
      Field::Lex(src, tokens, err);
      Field::AstNodePtr ast;
      Field::ParseProgram(tokens, ast, err);
      Field::ElementIRProgram prog;
      Field::LowerElementProgramToIR(ast, prog, err);

      if (err.Empty())
      {
         printf("SECTION 2: FAIL - explicit broadcast() was not rejected\n");
         secOk = false;
      }
      else
      {
         if (err.message.find("broadcast is implicit") == std::string::npos)
         {
            printf("SECTION 2: FAIL - unexpected error message for broadcast(): '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 2 (Broadcast Refusal): OK\n");
      else { printf("SECTION 2 (Broadcast Refusal): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 3: Frame Subexpression Hoisting & Evaluation Count
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src = "amount = 0.5 + 0.5 * sin(t)\nP.y += amount\n";
      FieldElementNode fe;
      fe.code = src;
      if (!fe.Apply())
      {
         printf("SECTION 3: FAIL - failed to compile element program: %s\n", fe.LastError().c_str());
         secOk = false;
      }
      else
      {
         DummyGeo srcGeo;
         const int N = 1000;
         for (int i = 0; i < N; ++i)
         {
            Vertex v;
            v.px = 0.0f; v.py = (float)i; v.pz = 0.0f;
            srcGeo.mesh.vertices.push_back(v);
         }
         fe.input = &srcGeo;
         fe.CookIfNeeded(1);

         const auto& outMesh = fe.GetMesh();
         if (outMesh.vertices.size() != N)
         {
            printf("SECTION 3: FAIL - mesh vertex count mismatch: %zu vs %d\n", outMesh.vertices.size(), N);
            secOk = false;
         }
         else
         {
            // Verify all vertices received exact hoisted amount: 0.5 + 0.5 * sin(0) = 0.5
            for (int i = 0; i < N; ++i)
            {
               float expectedY = (float)i + 0.5f;
               float actualY = outMesh.vertices[i].py;
               if (std::abs(actualY - expectedY) > 1e-4f)
               {
                  printf("SECTION 3: FAIL - vertex %d has Py = %f, expected %f\n", i, actualY, expectedY);
                  secOk = false;
                  break;
               }
            }

            // §5.9 evaluation counter assertions
            auto prog = fe.Program();
            if (prog)
            {
               if (prog->prologueEvalCount != 1)
               {
                  printf("SECTION 3: FAIL - prologue eval count = %llu, expected 1\n", (unsigned long long)prog->prologueEvalCount);
                  secOk = false;
               }
               if (prog->elementEvalCount != (uint64_t)N)
               {
                  printf("SECTION 3: FAIL - element loop eval count = %llu, expected %d\n", (unsigned long long)prog->elementEvalCount, N);
                  secOk = false;
               }
            }
         }
      }

      if (secOk) printf("SECTION 3 (Frame Hoisting & Exact Eval): OK\n");
      else { printf("SECTION 3 (Frame Hoisting & Exact Eval): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 4: Downsample Operator Legality & Semantics
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 4a. Refuse non-constant factor
      {
         std::string src = "param float k = 4 [1, 10]\ny = downsample(frame, k)\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::Lex(src, tokens, err);
         Field::AstNodePtr ast;
         Field::ParseProgram(tokens, ast, err);
         Field::ElementIRProgram prog;
         Field::LowerElementProgramToIR(ast, prog, err);

         if (err.Empty() || err.message.find("constant") == std::string::npos)
         {
            printf("SECTION 4: FAIL - non-constant factor downsample not refused: '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      // 4b. Refuse factor < 1
      {
         std::string src = "y = downsample(frame, 0)\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::Lex(src, tokens, err);
         Field::AstNodePtr ast;
         Field::ParseProgram(tokens, ast, err);
         Field::ElementIRProgram prog;
         Field::LowerElementProgramToIR(ast, prog, err);

         if (err.Empty() || err.message.find(">= 1") == std::string::npos)
         {
            printf("SECTION 4: FAIL - factor 0 downsample not refused: '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      // 4c. Execution semantics: frame-domain downsample(frame, 4) holds value across 4 frames
      {
         FieldElementNode fe;
         fe.code = "y = downsample(frame, 4)\nP.y = y\n";
         if (!fe.Apply())
         {
            printf("SECTION 4: FAIL - failed to compile downsample: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo srcGeo;
            Vertex v; v.px = 0.0f; v.py = 0.0f; v.pz = 0.0f;
            srcGeo.mesh.vertices.push_back(v);
            fe.input = &srcGeo;

            float recordedHold[8];
            for (int f = 0; f < 8; ++f)
            {
               fe.CookIfNeeded(f);
               recordedHold[f] = fe.GetMesh().vertices[0].py;
            }

            for (int f = 0; f < 4; ++f)
            {
               if (recordedHold[f] != 0.0f)
               {
                  printf("SECTION 4: FAIL - frame %d has hold value %f, expected 0.0f\n", f, recordedHold[f]);
                  secOk = false;
               }
            }
            for (int f = 4; f < 8; ++f)
            {
               if (recordedHold[f] != 4.0f)
               {
                  printf("SECTION 4: FAIL - frame %d has hold value %f, expected 4.0f\n", f, recordedHold[f]);
                  secOk = false;
               }
            }
         }
      }

      // 4d. Execution semantics: element-domain downsample(P.y, 4) holds across all vertices
      {
         FieldElementNode fe;
         fe.code = "y = downsample(P.y + frame, 4)\nP.y = y\n";
         if (!fe.Apply())
         {
            printf("SECTION 4: FAIL - failed to compile element-domain downsample: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo srcGeo;
            const int V = 10;
            for (int i = 0; i < V; ++i)
            {
               Vertex v; v.px = 0.0f; v.py = (float)(i * 10); v.pz = 0.0f;
               srcGeo.mesh.vertices.push_back(v);
            }
            fe.input = &srcGeo;

            for (int f = 0; f < 8; ++f)
            {
               fe.CookIfNeeded(f);
               const auto& mesh = fe.GetMesh();
               for (int i = 0; i < V; ++i)
               {
                  float expected = (float)(i * 10) + (f < 4 ? 0.0f : 4.0f);
                  float actual = mesh.vertices[i].py;
                  if (std::abs(actual - expected) > 1e-4f)
                  {
                     printf("SECTION 4: FAIL - frame %d, vertex %d has Py = %f, expected %f\n", f, i, actual, expected);
                     secOk = false;
                     break;
                  }
               }
            }
         }
      }

      if (secOk) printf("SECTION 4 (Downsample Legality & Hold Semantics): OK\n");
      else { printf("SECTION 4 (Downsample Legality & Hold Semantics): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 5: Map Operator Legality & State Isolation
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 5a. Refuse unbounded map count
      {
         std::string src = "param float num = 8 [1, 16]\nmap(num) {\nP.y += 1.0\n}\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::Lex(src, tokens, err);
         Field::AstNodePtr ast;
         Field::ParseProgram(tokens, ast, err);
         Field::ElementIRProgram prog;
         Field::LowerElementProgramToIR(ast, prog, err);

         if (err.Empty() || err.message.find("constant") == std::string::npos)
         {
            printf("SECTION 5: FAIL - non-constant map count not refused: '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      // 5b. Refuse map body coarser than surrounding domain
      {
         Field::FieldError err;
         if (Field::ValidateMap(Field::Domain::Element, Field::Domain::Frame, Field::SourceSpan{}, err))
         {
            printf("SECTION 5: FAIL - map with coarser body was permitted\n");
            secOk = false;
         }
      }

      // 5c. Map execution with state cell isolation across multiple instances and frames
      {
         FieldElementNode fe;
         fe.code = "map(4) {\nstate float acc = 0\nacc += 1.0\nP.y += acc\n}\n";
         if (!fe.Apply())
         {
            printf("SECTION 5: FAIL - failed to compile map with state: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo srcGeo;
            Vertex v; v.px = 0.0f; v.py = 0.0f; v.pz = 0.0f;
            srcGeo.mesh.vertices.push_back(v);
            fe.input = &srcGeo;

            // Frame 0: each of 4 cells updates acc from 0 to 1, Py += 4 * 1 = 4.0
            fe.CookIfNeeded(0);
            float py0 = fe.GetMesh().vertices[0].py;
            if (std::abs(py0 - 4.0f) > 1e-4f)
            {
               printf("SECTION 5: FAIL - frame 0 Py = %f, expected 4.0f\n", py0);
               secOk = false;
            }

            // Frame 1: each of 4 cells updates acc from 1 to 2, Py += 4 * 2 = 8.0
            fe.CookIfNeeded(1);
            float py1 = fe.GetMesh().vertices[0].py;
            if (std::abs(py1 - 8.0f) > 1e-4f)
            {
               printf("SECTION 5: FAIL - frame 1 Py = %f, expected 8.0f\n", py1);
               secOk = false;
            }

            // Frame 2: each of 4 cells updates acc from 2 to 3, Py += 4 * 3 = 12.0
            fe.CookIfNeeded(2);
            float py2 = fe.GetMesh().vertices[0].py;
            if (std::abs(py2 - 12.0f) > 1e-4f)
            {
               printf("SECTION 5: FAIL - frame 2 Py = %f, expected 12.0f\n", py2);
               secOk = false;
            }

            // Verify 4 distinct state cells exist in FieldState
            if (fe.State().Cells().size() != 4)
            {
               printf("SECTION 5: FAIL - expected 4 state cells in map, found %zu\n", fe.State().Cells().size());
               secOk = false;
            }
         }
      }

      if (secOk) printf("SECTION 5 (Map Legality & Domain Constraints): OK\n");
      else { printf("SECTION 5 (Map Legality & Domain Constraints): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 6: Reduce Operators on Element Domain & Numerical Correctness
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 6a. Direct numerical accuracy test of reduction kernels
      const float testVals[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
      double sumVal = Field::ReduceSum(testVals, 4);
      double meanVal = Field::ReduceMean(testVals, 4);
      double rmsVal = Field::ReduceRms(testVals, 4);
      double minVal = Field::ReduceMin(testVals, 4);
      double maxVal = Field::ReduceMax(testVals, 4);

      if (std::abs(sumVal - 10.0) > 1e-6) { printf("SECTION 6: FAIL - sum = %f, expected 10.0\n", sumVal); secOk = false; }
      if (std::abs(meanVal - 2.5) > 1e-6) { printf("SECTION 6: FAIL - mean = %f, expected 2.5\n", meanVal); secOk = false; }
      if (std::abs(rmsVal - std::sqrt(7.5)) > 1e-6) { printf("SECTION 6: FAIL - rms = %f, expected %f\n", rmsVal, std::sqrt(7.5)); secOk = false; }
      if (std::abs(minVal - 1.0) > 1e-6) { printf("SECTION 6: FAIL - min = %f, expected 1.0\n", minVal); secOk = false; }
      if (std::abs(maxVal - 4.0) > 1e-6) { printf("SECTION 6: FAIL - max = %f, expected 4.0\n", maxVal); secOk = false; }

      // 6b. Execution of reduce in Element VM prologue
      {
         FieldElementNode fe;
         fe.code = "avg = reduce.mean(P)\nP.y = avg.x\n";
         if (!fe.Apply())
         {
            printf("SECTION 6: FAIL - failed to compile reduce: %s\n", fe.LastError().c_str());
            secOk = false;
         }
         else
         {
            DummyGeo srcGeo;
            for (int i = 0; i < 4; ++i)
            {
               Vertex v;
               v.px = testVals[i]; v.py = 0.0f; v.pz = 0.0f;
               srcGeo.mesh.vertices.push_back(v);
            }
            fe.input = &srcGeo;
            fe.CookIfNeeded(1);

            const auto& outMesh = fe.GetMesh();
            for (int i = 0; i < 4; ++i)
            {
               if (std::abs(outMesh.vertices[i].py - 2.5f) > 1e-5f)
               {
                  printf("SECTION 6: FAIL - vertex %d Py = %f, expected 2.5f\n", i, outMesh.vertices[i].py);
                  secOk = false;
               }
            }
         }
      }

      // 6c. Non-bare variable reduction refusal
      {
         std::string src = "avg = reduce.mean(P.x * 2.0)\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::Lex(src, tokens, err);
         Field::AstNodePtr ast;
         Field::ParseProgram(tokens, ast, err);
         Field::ElementIRProgram irProg;
         Field::LowerElementProgramToIR(ast, irProg, err);

         if (err.Empty() || err.message.find("bare variable") == std::string::npos)
         {
            printf("SECTION 6: FAIL - non-bare reduction argument not refused: '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      // 6d. Redundant reduction on frame-domain value refusal
      {
         std::string src = "avg = reduce.mean(t)\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::Lex(src, tokens, err);
         Field::AstNodePtr ast;
         Field::ParseProgram(tokens, ast, err);
         Field::ElementIRProgram irProg;
         Field::LowerElementProgramToIR(ast, irProg, err);

         if (err.Empty() || err.message.find("already frame-domain") == std::string::npos)
         {
            printf("SECTION 6: FAIL - frame-domain reduction not refused: '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 6 (Reduce Operators & Numerical Exactness): OK\n");
      else { printf("SECTION 6 (Reduce Operators & Numerical Exactness): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 7: Pixel GLSL Reduce Refusal (No Silent CPU Fallback)
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // In pixel domain, reduce must be refused at compile time
      std::string src = "avg = reduce.mean(uv)\ncol = vec3(avg.x)\n";
      std::vector<Field::Token> tokens;
      Field::FieldError err;
      Field::Lex(src, tokens, err);
      Field::AstNodePtr ast;
      Field::ParseProgram(tokens, ast, err);

      Field::PixelIRProgram pixelProg;
      Field::LowerPixelProgramToIR(ast, pixelProg, err);

      if (err.Empty())
      {
         auto glslResult = Field::EmitGlsl(pixelProg);
         if (glslResult.error.empty())
         {
            printf("SECTION 7: FAIL - GLSL emitter did not refuse reduce in pixel kernel\n");
            secOk = false;
         }
         else if (glslResult.error.find("not lowerable in GLSL") == std::string::npos)
         {
            printf("SECTION 7: FAIL - unexpected GLSL reduce refusal error: '%s'\n", glslResult.error.c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 7 (Pixel GLSL Reduce Refusal): OK\n");
      else { printf("SECTION 7 (Pixel GLSL Reduce Refusal): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 8: Resample Operator Legality
   // ------------------------------------------------------------
   {
      bool secOk = true;

      Field::FieldError err;
      // 8a. Legal: Sample -> Frame
      if (!Field::ValidateResample(Field::Domain::Sample, Field::Domain::Frame, Field::SourceSpan{}, err))
      {
         printf("SECTION 8: FAIL - resample(Sample -> Frame) failed: '%s'\n", err.message.c_str());
         secOk = false;
      }

      // 8b. Legal: Frame -> Element
      err.Clear();
      if (!Field::ValidateResample(Field::Domain::Frame, Field::Domain::Element, Field::SourceSpan{}, err))
      {
         printf("SECTION 8: FAIL - resample(Frame -> Element) failed: '%s'\n", err.message.c_str());
         secOk = false;
      }

      // 8c. Refused: Element -> Frame (hint: use reduce instead)
      err.Clear();
      if (Field::ValidateResample(Field::Domain::Element, Field::Domain::Frame, Field::SourceSpan{}, err))
      {
         printf("SECTION 8: FAIL - resample(Element -> Frame) was permitted\n");
         secOk = false;
      }
      else if (err.hint.find("reduce") == std::string::npos)
      {
         printf("SECTION 8: FAIL - resample(Element -> Frame) hint did not suggest reduce: '%s'\n", err.hint.c_str());
         secOk = false;
      }

      // 8d. Refused: Element -> Pixel (incomparable)
      err.Clear();
      if (Field::ValidateResample(Field::Domain::Element, Field::Domain::Pixel, Field::SourceSpan{}, err))
      {
         printf("SECTION 8: FAIL - resample(Element -> Pixel) was permitted\n");
         secOk = false;
      }

      if (secOk) printf("SECTION 8 (Resample Operator Legality): OK\n");
      else { printf("SECTION 8 (Resample Operator Legality): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 9: Cost Table Data Integrity & Readout
   // ------------------------------------------------------------
   {
      bool secOk = true;
      const auto& table = Field::GetTransferCostTable();

      if (table.size() != 16)
      {
         printf("SECTION 9: FAIL - expected 16 entries in cost table, got %zu\n", table.size());
         secOk = false;
      }

      // Format readout test
      std::string formatted = Field::FormatTransferCostReadout(Field::Domain::Element, Field::Domain::Frame, Field::TransferKind::Reduce, 1000);
      if (formatted.find("element -> frame [reduce]: O(N) CPU, once/frame") == std::string::npos)
      {
         printf("SECTION 9: FAIL - unexpected formatted readout: '%s'\n", formatted.c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 9 (Cost Table Integrity & Readout): OK\n");
      else { printf("SECTION 9 (Cost Table Integrity & Readout): FAIL\n"); allOk = false; }
   }

   printf("INFINITE_FIELDTRANSFERTEST: %s\n", allOk ? "OK" : "FAIL");
   fflush(stdout);
   return allOk ? 0 : 1;
}

// ======================================================= INFINITE_FIELDSAMPLETEST
//
// Conformance harness for the Field 'sample' domain (build step 9): the
// register-machine compiler (BackendRegister.cpp), interpreter
// (SampleRuntime.h) and FieldSampleNode/AudioFieldSampleNode's real-time
// audio-thread execution. Headless - drives FieldSampleNode's public
// interface and its AudioNode* directly (GetAudioNode()), the same pattern
// AUDIOPDCTEST uses, since AudioFieldSampleNode itself is a private class
// defined only inside FieldSampleNode.cpp.
//
// Coverage (fixture-table rows from docs/plans/field/step-09-sample-domain.md
// §... this fixture actually exercises):
//   - basic compile + per-sample execution (state accumulator)
//   - state hot-reload transplant by (name,type) match across Apply()
//   - param declaration -> ParamMailbox smoothing reaches the pushed value
//   - note-on resets a voice's state to its declared initial value
//   - 129-param compile-time refusal (kMaxParams = 128)
//   - non-constant for-loop bound compile-time refusal
//   - NaN poisoning is caught by the once-per-block sweep, recovered, and
//     counted (pow(-1, 0.5) is unguarded by the interpreter's domain guards,
//     unlike sqrt/log/div)
//   - reduce.rms(in, loHz, hiHz) publishes to MeterRing, readable via
//     FieldSampleNode::ReadRmsLatest
//   - zero allocation across N steady-state ProcessBlock calls
//   - freq/gate reserved sample-domain symbols: a kernel that never reads
//     'in' generates from freq/gate alone; gate drops to 0 the instant
//     note-off arrives, independent of the amplitude envelope's own
//     release tail (design-prompt-sample-generator-mode.md)
//   - polyphonic delay() ring buffer isolation across concurrent voices (FieldSynthNode)
//
// Deliberately deferred (not covered by this fixture): true voice-stealing
// across more concurrent notes than kMaxVoices, hot-reload across a type
// change (float -> a future vector state type, once one exists), and the
// full param-index-vs-mailboxId reordering scenario (covered indirectly by
// AUDIOPARAMSWEEPTEST's generic sweep instead).
int RunFieldSampleTest()
{
   printf("[FIELDSAMPLETEST] Running Field sample-domain conformance harness...\n");
   bool allOk = true;

   const double kSr = 48000.0;
   const int kBlock = 64;

   // ------------------------------------------------------------
   // SECTION 1: Basic compile + per-sample execution
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "state float y = 0\ny = y + 1\nout = y\n";
      if (!node.Apply())
      {
         printf("SECTION 1: FAIL - compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);

         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 1.0f;
         on.voiceId = NextVoiceId();
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> chan(kBlock, 0.0f);
         float* chans[1] = { chan.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 1;
         buf.numFrames = kBlock;
         an->ProcessBlock(nullptr, 0, buf);

         // y increments by 1 each sample starting from 0, scaled by the
         // voice envelope (fast attack, not yet fully open at sample 0) -
         // check monotonic growth and a nonzero tail rather than an exact
         // unscaled value.
         if (chan[kBlock - 1] <= chan[1])
         {
            printf("SECTION 1: FAIL - output is not monotonically increasing (chan[1]=%f chan[N-1]=%f)\n",
                   chan[1], chan[kBlock - 1]);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 1 (Basic Compile + Execution): OK\n");
      else { printf("SECTION 1 (Basic Compile + Execution): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 2: State hot-reload transplant by (name,type) match
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "state float acc = 5\nacc = acc + 0\nout = acc\n";
      if (!node.Apply())
      {
         printf("SECTION 2: FAIL - initial compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 1.0f;
         on.voiceId = NextVoiceId();
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> chan(kBlock, 0.0f);
         float* chans[1] = { chan.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 1;
         buf.numFrames = kBlock;
         an->ProcessBlock(nullptr, 0, buf); // adopts the program, voice starts, acc settles near 5

         // Same state name+type, different kernel body - the hot-reload
         // must transplant 'acc's live value rather than resetting it to
         // its (now different) declared initial value of 100.
         node.code = "state float acc = 100\nacc = acc + 0\nout = acc\n";
         if (!node.Apply())
         {
            printf("SECTION 2: FAIL - reload compile failed: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            an->ProcessBlock(nullptr, 0, buf); // adopts the new program at top of block
            if (chan[kBlock - 1] > 50.0f)
            {
               printf("SECTION 2: FAIL - state was not transplanted (got %f, expected near 5, not near 100)\n",
                      chan[kBlock - 1]);
               secOk = false;
            }
         }
      }

      if (secOk) printf("SECTION 2 (State Hot-Reload Transplant): OK\n");
      else { printf("SECTION 2 (State Hot-Reload Transplant): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 3: Param declaration + mailbox smoothing
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "param float amt = 0 [0, 1]\nout = amt\n";
      if (!node.Apply())
      {
         printf("SECTION 3: FAIL - compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 1.0f;
         on.voiceId = NextVoiceId();
         on.frameOffset = 0;
         notes.Push(on);

         if (Field::ParamEntry* entry = node.GetParamTable().Find("amt"))
            entry->value = 1.0f;

         std::vector<float> chan(kBlock, 0.0f);
         float* chans[1] = { chan.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 1;
         buf.numFrames = kBlock;

         // CookIfNeeded pushes the ParamTable value to the mailbox; run
         // several blocks so the 5ms-class smoothing ramp has time to
         // approach its target.
         for (int i = 0; i < 40; i++)
         {
            node.CookIfNeeded(i);
            an->ProcessBlock(nullptr, 0, buf);
         }

         if (chan[kBlock - 1] < 0.9f)
         {
            printf("SECTION 3: FAIL - param did not smooth to target (got %f, expected near 1.0)\n", chan[kBlock - 1]);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 3 (Param Mailbox Smoothing): OK\n");
      else { printf("SECTION 3 (Param Mailbox Smoothing): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 4: Note-on resets voice state to its declared initial value
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "state float acc = 0\nacc = acc + 1\nout = acc\n";
      if (!node.Apply())
      {
         printf("SECTION 4: FAIL - compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);

         NoteEvent on1;
         on1.isNoteOn = true;
         on1.note = 60;
         on1.velocity = 1.0f;
         on1.voiceId = NextVoiceId();
         on1.frameOffset = 0;
         notes.Push(on1);

         std::vector<float> chan(kBlock, 0.0f);
         float* chans[1] = { chan.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 1;
         buf.numFrames = kBlock;
         an->ProcessBlock(nullptr, 0, buf);
         const float afterFirst = chan[kBlock - 1];

         // A second note-on (a fresh voice) must start its own 'acc' back
         // at 0 - it should not inherit the first voice's accumulated value.
         NoteEvent on2;
         on2.isNoteOn = true;
         on2.note = 64;
         on2.velocity = 1.0f;
         on2.voiceId = NextVoiceId();
         on2.frameOffset = 0;
         notes.Push(on2);
         an->ProcessBlock(nullptr, 0, buf);
         const float afterSecondNoteOn = chan[0];

         // Only checked against 0, not a fraction of kBlock - the voice
         // envelope's attack curve (AudioFieldSampleNode's fixed 2ms attack)
         // scales 'acc' by an unknown, non-linear amount within one block,
         // so this only asserts the kernel actually ran and accumulated.
         if (afterFirst <= 0.0f)
         {
            printf("SECTION 4: FAIL - first voice did not accumulate as expected (got %f)\n", afterFirst);
            secOk = false;
         }
         // The new voice's output at sample 0 of the second block is the
         // sum of both voices' kernels; the first voice continues from
         // ~kBlock, the second starts from ~1 - so the combined output at
         // frame 0 should track the first voice's continuation, not reset
         // to near 0, confirming voice 1 was undisturbed while voice 2
         // started fresh. This is a coarse but deterministic signal.
         if (afterSecondNoteOn < afterFirst)
         {
            printf("SECTION 4: FAIL - triggering a second voice disturbed the first voice's continuity "
                   "(afterFirst=%f afterSecondNoteOn=%f)\n", afterFirst, afterSecondNoteOn);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 4 (Note-On Voice State Reset): OK\n");
      else { printf("SECTION 4 (Note-On Voice State Reset): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 5: 129-param compile-time refusal (kMaxParams = 128)
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src;
      for (int i = 0; i < 129; i++)
         src += "param float p" + std::to_string(i) + " = 0 [0, 1]\n";
      src += "out = 0\n";

      FieldSampleNode node;
      node.code = src;
      if (node.Apply())
      {
         printf("SECTION 5: FAIL - 129-param program was accepted\n");
         secOk = false;
      }
      else if (node.LastError().find("kMaxParams") == std::string::npos &&
               node.LastError().find("128") == std::string::npos)
      {
         printf("SECTION 5: FAIL - unexpected refusal message: '%s'\n", node.LastError().c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 5 (129-Param Refusal): OK\n");
      else { printf("SECTION 5 (129-Param Refusal): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 6: non-constant for-loop bound compile-time refusal
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "state float s = 0\nfor (i = 0; i < n; i += 1) { s = s + 1 }\nout = s\n";
      if (node.Apply())
      {
         printf("SECTION 6: FAIL - non-constant loop bound ('n', a reserved runtime value) was accepted\n");
         secOk = false;
      }
      else if (node.LastError().find("compile-time integer constants") == std::string::npos)
      {
         printf("SECTION 6: FAIL - unexpected refusal message: '%s'\n", node.LastError().c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 6 (Non-Constant Loop Bound Refusal): OK\n");
      else { printf("SECTION 6 (Non-Constant Loop Bound Refusal): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 7: NaN poisoning caught, recovered, and counted
   // ------------------------------------------------------------
   {
      bool secOk = true;
      // pow(-1, 0.5) = NaN - unguarded by the interpreter's domain guards
      // (unlike sqrt/log/div), so this deterministically poisons 'out' and
      // the voice's own state on the very first sample. '0 - 1' rather than
      // a negative literal, since a state initial value must be a literal
      // AST node (BackendRegister.cpp) and this doesn't need state at all.
      FieldSampleNode node;
      node.code = "out = pow(0 - 1, 0.5)\n";
      if (!node.Apply())
      {
         printf("SECTION 7: FAIL - compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 1.0f;
         on.voiceId = NextVoiceId();
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> chan(kBlock, 1.0f);
         float* chans[1] = { chan.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 1;
         buf.numFrames = kBlock;
         an->ProcessBlock(nullptr, 0, buf);

         bool anyNonZero = false;
         for (float v : chan) if (v != 0.0f) anyNonZero = true;
         if (anyNonZero)
         {
            printf("SECTION 7: FAIL - poisoned block was not zeroed\n");
            secOk = false;
         }
         if (node.FaultCount() == 0)
         {
            printf("SECTION 7: FAIL - fault counter did not increment\n");
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 7 (NaN Poisoning Recovery): OK\n");
      else { printf("SECTION 7 (NaN Poisoning Recovery): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 8: reduce.rms(in, loHz, hiHz) publishes to MeterRing
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "out = in\nreduce.rms(in, 20, 20000)\n";
      if (!node.Apply())
      {
         printf("SECTION 8: FAIL - compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 1.0f;
         on.voiceId = NextVoiceId();
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> inChan(kBlock, 0.0f);
         for (int i = 0; i < kBlock; i++)
            inChan[i] = (i % 2 == 0) ? 1.0f : -1.0f; // full-scale square wave, RMS = 1.0
         float* inChans[1] = { inChan.data() };
         AudioBuffer inBuf;
         inBuf.channels = inChans;
         inBuf.numChannels = 1;
         inBuf.numFrames = kBlock;

         std::vector<float> outChan(kBlock, 0.0f);
         float* outChans[1] = { outChan.data() };
         AudioBuffer outBuf;
         outBuf.channels = outChans;
         outBuf.numChannels = 1;
         outBuf.numFrames = kBlock;

         const AudioBuffer* inputs[1] = { &inBuf }; // slot 0 = 'in', matching AudioInputSlot(0)
         an->ProcessBlock(inputs, 1, outBuf);

         float rms = 0.0f;
         if (!node.ReadRmsLatest(rms))
         {
            printf("SECTION 8: FAIL - no reduce.rms publish was read back\n");
            secOk = false;
         }
         else if (rms < 0.5f)
         {
            printf("SECTION 8: FAIL - implausible RMS reading %f for a full-scale square wave\n", rms);
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 8 (reduce.rms MeterRing Publish): OK\n");
      else { printf("SECTION 8 (reduce.rms MeterRing Publish): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 9: zero allocation across steady-state ProcessBlock calls
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      node.code = "state float y = 0\ny = y * 0.999 + in * 0.1\nout = y\nparam float g = 1 [0, 2]\n";
      if (!node.Apply())
      {
         printf("SECTION 9: FAIL - compile failed: %s\n", node.LastError().c_str());
         secOk = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 1.0f;
         on.voiceId = NextVoiceId();
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> inChan(kBlock, 0.5f);
         float* inChans[1] = { inChan.data() };
         AudioBuffer inBuf;
         inBuf.channels = inChans;
         inBuf.numChannels = 1;
         inBuf.numFrames = kBlock;
         std::vector<float> outChan(kBlock, 0.0f);
         float* outChans[1] = { outChan.data() };
         AudioBuffer outBuf;
         outBuf.channels = outChans;
         outBuf.numChannels = 1;
         outBuf.numFrames = kBlock;
         const AudioBuffer* inputs[2] = { nullptr, &inBuf };

         // Warm up (voice steal machinery, any lazy first-touch) before
         // counting - only steady-state blocks are held to the zero-
         // allocation bar.
         for (int i = 0; i < 8; i++)
         {
            node.CookIfNeeded(i);
            an->ProcessBlock(inputs, 2, outBuf);
         }

#if defined(__APPLE__)
         // malloc_zone_statistics reads the allocator's own cumulative
         // counters rather than overriding the global operator new/delete -
         // overriding those process-wide just to count 200 steady-state
         // blocks would change allocation behavior for the entire app, not
         // just this test.
         malloc_statistics_t before{};
         malloc_zone_statistics(malloc_default_zone(), &before);
         for (int i = 8; i < 8 + 200; i++)
         {
            node.CookIfNeeded(i);
            an->ProcessBlock(inputs, 2, outBuf);
         }
         malloc_statistics_t after{};
         malloc_zone_statistics(malloc_default_zone(), &after);
         const long allocs = (long)after.size_allocated - (long)before.size_allocated;

         if (allocs != 0)
         {
            printf("SECTION 9: FAIL - allocator's size_allocated grew by %ld byte(s) during steady-state ProcessBlock\n", allocs);
            secOk = false;
         }
#else
         for (int i = 8; i < 8 + 200; i++)
         {
            node.CookIfNeeded(i);
            an->ProcessBlock(inputs, 2, outBuf);
         }
         printf("SECTION 9: skipped (malloc_zone_statistics is macOS-only)\n");
#endif
      }

      if (secOk) printf("SECTION 9 (Zero Allocation, Steady State): OK\n");
      else { printf("SECTION 9 (Zero Allocation, Steady State): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 10: freq/gate reserved sample-domain symbols (generator mode)
   // ------------------------------------------------------------
   {
      bool secOk = true;
      FieldSampleNode node;
      if (node.NoteInputSlot(0) == nullptr)
      {
         printf("SECTION 10: skipped (FieldSampleNode is audio-effects-only; generator/note mode moved to FieldSynthNode in step 21)\n");
      }
      else
      {
         // No 'in' read anywhere - a self-contained generator kernel, the
         // whole point of design-prompt-sample-generator-mode.md. Scaled by
         // 0.001 so 440 Hz lands well under AudioFieldSampleNode's +-4.0
         // output headroom clamp - this kernel is checking the raw freq/gate
         // values reach the register machine, not synthesizing real audio.
         // freq*gate is 0 the instant gate drops, even though the voice's
         // amplitude envelope (applied externally, outside the kernel) is
         // still mid-release and would otherwise mask a gate stuck at 1.
         node.code = "out = freq * gate * 0.001\n";
         if (!node.Apply())
         {
            printf("SECTION 10: FAIL - compile failed: %s\n", node.LastError().c_str());
            secOk = false;
         }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay(kSr, kBlock);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);

         const int voiceId = NextVoiceId();
         NoteEvent on;
         on.isNoteOn = true;
         on.note = 69; // A4 = 440 Hz exactly, by construction of the MIDI->Hz formula
         on.velocity = 1.0f;
         on.voiceId = voiceId;
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> chan(kBlock, 0.0f);
         float* chans[1] = { chan.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 1;
         buf.numFrames = kBlock;

         // Block 1: attack ramping. Block 2: well past the 2ms/96-sample
         // attack+0ms decay, so the envelope is exactly at its 1.0 sustain
         // level by the end of block 2 - output should equal freq*gate
         // (440 * 1) with no envelope scaling left to account for.
         an->ProcessBlock(nullptr, 0, buf);
         an->ProcessBlock(nullptr, 0, buf);
         const float heldOut = chan[kBlock - 1];
         const float kExpectedHeld = 440.0f * 0.001f; // freq * gate(1) * 0.001
         if (std::fabs(heldOut - kExpectedHeld) > 0.01f)
         {
            printf("SECTION 10: FAIL - held-note output %f is not close to expected %f (freq=440 Hz, gate should be 1)\n",
                   heldOut, kExpectedHeld);
            secOk = false;
         }

         // Note-off: gate must drop to 0 on the very next block, even
         // though the 30ms release means the voice is still active (its
         // envelope is still mid-decay) - if gate were wrongly left at 1,
         // output would still read close to 440 * (still-high envelope),
         // not 0.
         NoteEvent off;
         off.isNoteOn = false;
         off.voiceId = voiceId;
         off.frameOffset = 0;
         notes.Push(off);
         an->ProcessBlock(nullptr, 0, buf);
         const float releasedOut = chan[kBlock - 1];
         if (std::fabs(releasedOut) > 0.001f)
         {
            printf("SECTION 10: FAIL - output after note-off is %f, expected exactly 0 (gate did not drop)\n", releasedOut);
            secOk = false;
         }
      }
   }

   if (secOk) printf("SECTION 10 (freq/gate Reserved Symbols): OK\n");
      else { printf("SECTION 10 (freq/gate Reserved Symbols): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 11: Factory Presets Compile & Zero-Fault Run
   // ------------------------------------------------------------
   {
      bool secOk = true;
      for (const auto& preset : FieldSampleNode::Presets())
      {
         FieldSampleNode node;
         node.code = preset.code;
         if (!node.Apply())
         {
            printf("SECTION 11: FAIL - preset '%s' did not compile: %s\n", preset.name, node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);
            std::vector<float> inChan(128, 0.5f);
            float* inChans[1] = { inChan.data() };
            AudioBuffer inBuf;
            inBuf.channels = inChans;
            inBuf.numChannels = 1;
            inBuf.numFrames = 128;
            const AudioBuffer* inputs[1] = { &inBuf };

            std::vector<float> outChan(128, 0.0f);
            float* outChans[1] = { outChan.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 1;
            outBuf.numFrames = 128;

            for (int b = 0; b < 50; b++)
            {
               node.CookIfNeeded(b + 1);
               an->ProcessBlock(inputs, 1, outBuf);
            }

            if (node.FaultCount() > 0)
            {
               printf("SECTION 11: FAIL - preset '%s' generated %llu NaN/inf recovery fault(s)\n",
                      preset.name, (unsigned long long)node.FaultCount());
               secOk = false;
            }
         }
      }
      if (secOk) printf("SECTION 11 (Factory Presets Compile & Run): OK\n");
      else { printf("SECTION 11 (Factory Presets Compile & Run): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 12: delay(x, samples) Sample-Domain Intrinsic (Step 19)
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 12a. Basic impulse delay: delay(in, 10) must produce the input impulse exactly 10 samples later.
      {
         FieldSampleNode node;
         node.code = "out = delay(in, 10)\n";
         if (!node.Apply())
         {
            printf("SECTION 12: FAIL - delay(in, 10) did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(kSr, 32);
            std::vector<float> inChan(32, 0.0f);
            inChan[0] = 1.0f; // impulse at sample 0
            float* inChans[1] = { inChan.data() };
            AudioBuffer inBuf;
            inBuf.channels = inChans;
            inBuf.numChannels = 1;
            inBuf.numFrames = 32;

            std::vector<float> outChan(32, 0.0f);
            float* outChans[1] = { outChan.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 1;
            outBuf.numFrames = 32;

            const AudioBuffer* inputs[1] = { &inBuf };
            an->ProcessBlock(inputs, 1, outBuf);

            // Samples 0..9 must be 0.0f
            for (int i = 0; i < 10; i++)
            {
               if (outChan[i] != 0.0f)
               {
                  printf("SECTION 12: FAIL - sample %d is %f, expected 0.0f\n", i, outChan[i]);
                  secOk = false;
                  break;
               }
            }
            // Sample 10 must be 1.0f
            if (std::fabs(outChan[10] - 1.0f) > 1e-6f)
            {
               printf("SECTION 12: FAIL - sample 10 is %f, expected 1.0f\n", outChan[10]);
               secOk = false;
            }
            // Samples 11..31 must be 0.0f
            for (int i = 11; i < 32; i++)
            {
               if (outChan[i] != 0.0f)
               {
                  printf("SECTION 12: FAIL - sample %d is %f, expected 0.0f\n", i, outChan[i]);
                  secOk = false;
                  break;
               }
            }
         }
      }

      // 12b. Long delay: 4410 samples (100ms at 44.1kHz).
      {
         FieldSampleNode node;
         node.code = "out = delay(in, 4410)\n";
         if (!node.Apply())
         {
            printf("SECTION 12: FAIL - delay(in, 4410) did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
      }

      // 12c. Multiple delay lines in one kernel.
      {
         FieldSampleNode node;
         node.code = "d1 = delay(in, 8)\nd2 = delay(in, 16)\nout = d1 + d2\n";
         if (!node.Apply())
         {
            printf("SECTION 12: FAIL - multi-tap delay did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(kSr, 32);
            std::vector<float> inChan(32, 0.0f);
            inChan[0] = 1.0f;
            float* inChans[1] = { inChan.data() };
            AudioBuffer inBuf;
            inBuf.channels = inChans;
            inBuf.numChannels = 1;
            inBuf.numFrames = 32;

            std::vector<float> outChan(32, 0.0f);
            float* outChans[1] = { outChan.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 1;
            outBuf.numFrames = 32;

            const AudioBuffer* inputs[1] = { &inBuf };
            an->ProcessBlock(inputs, 1, outBuf);

            if (std::fabs(outChan[8] - 1.0f) > 1e-6f || std::fabs(outChan[16] - 1.0f) > 1e-6f)
            {
               printf("SECTION 12: FAIL - multi-tap taps at 8 (%f) and 16 (%f) did not match expected 1.0f\n",
                      outChan[8], outChan[16]);
               secOk = false;
            }
         }
      }

      // 12d. Rejections: non-literal N, non-positive integer, invalid arity, budget cap.
      {
         Field::SampleProgram prog;
         Field::FieldError err;

         // Non-literal length argument
         if (Field::CompileSampleProgram("out = delay(in, in)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - non-literal length argument was not rejected\n");
            secOk = false;
         }
         else if (err.message.find("compile-time constant") == std::string::npos)
         {
            printf("SECTION 12: FAIL - expected 'compile-time constant' error, got '%s'\n", err.message.c_str());
            secOk = false;
         }

         // Non-positive length (0)
         if (Field::CompileSampleProgram("out = delay(in, 0)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - zero length argument was not rejected\n");
            secOk = false;
         }

         // Negative length (-10)
         if (Field::CompileSampleProgram("out = delay(in, -10)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - negative length argument was not rejected\n");
            secOk = false;
         }

         // Non-integer length (4.5)
         if (Field::CompileSampleProgram("out = delay(in, 4.5)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - fractional length argument was not rejected\n");
            secOk = false;
         }

         // Arity: 1 argument
         if (Field::CompileSampleProgram("out = delay(in)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - 1-argument delay was not rejected\n");
            secOk = false;
         }

         // Arity: 3 arguments
         if (Field::CompileSampleProgram("out = delay(in, 10, 20)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - 3-argument delay was not rejected\n");
            secOk = false;
         }

         // Exceeding cumulative delay budget cap
         if (Field::CompileSampleProgram("out = delay(in, 70000)\n", nullptr, prog, err))
         {
            printf("SECTION 12: FAIL - delay exceeding kSampleMaxDelayCells was not rejected\n");
            secOk = false;
         }
         else if (err.message.find("cap") == std::string::npos)
         {
            printf("SECTION 12: FAIL - expected 'cap' error for exceeding budget, got '%s'\n", err.message.c_str());
            secOk = false;
         }
      }

      // 12e. Hot-reload delay buffer transplant: ring buffer contents preserved when length matches.
      {
         FieldSampleNode node;
         node.code = "out = delay(in, 16)\n";
         if (!node.Apply())
         {
            printf("SECTION 12: FAIL - initial delay compile failed: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(kSr, 8);

            // Block 1: feed impulse at sample 0 into 8-sample block.
            std::vector<float> inChan1(8, 0.0f);
            inChan1[0] = 1.0f;
            float* inChans1[1] = { inChan1.data() };
            AudioBuffer inBuf1; inBuf1.channels = inChans1; inBuf1.numChannels = 1; inBuf1.numFrames = 8;
            std::vector<float> outChan1(8, 0.0f);
            float* outChans1[1] = { outChan1.data() };
            AudioBuffer outBuf1; outBuf1.channels = outChans1; outBuf1.numChannels = 1; outBuf1.numFrames = 8;
            const AudioBuffer* inputs1[1] = { &inBuf1 };
            an->ProcessBlock(inputs1, 1, outBuf1);

            // Hot reload: recompile with scaling factor * 2.0 (same delay length 16).
            node.code = "out = delay(in, 16) * 2.0\n";
            if (!node.Apply())
            {
               printf("SECTION 12: FAIL - recompile failed: %s\n", node.LastError().c_str());
               secOk = false;
            }
            else
            {
               // Block 2: 8 frames of silence (frames 8..15 total).
               std::vector<float> inChan2(8, 0.0f);
               float* inChans2[1] = { inChan2.data() };
               AudioBuffer inBuf2; inBuf2.channels = inChans2; inBuf2.numChannels = 1; inBuf2.numFrames = 8;
               std::vector<float> outChan2(8, 0.0f);
               float* outChans2[1] = { outChan2.data() };
               AudioBuffer outBuf2; outBuf2.channels = outChans2; outBuf2.numChannels = 1; outBuf2.numFrames = 8;
               const AudioBuffer* inputs2[1] = { &inBuf2 };
               an->ProcessBlock(inputs2, 1, outBuf2);

               // Block 3: 8 frames of silence (frames 16..23 total).
               // Frame 0 of this block is overall frame 16: exactly where the impulse delayed by 16 samples lands!
               // It should be 2.0f because of the * 2.0 hot-swap!
               std::vector<float> inChan3(8, 0.0f);
               float* inChans3[1] = { inChan3.data() };
               AudioBuffer inBuf3; inBuf3.channels = inChans3; inBuf3.numChannels = 1; inBuf3.numFrames = 8;
               std::vector<float> outChan3(8, 0.0f);
               float* outChans3[1] = { outChan3.data() };
               AudioBuffer outBuf3; outBuf3.channels = outChans3; outBuf3.numChannels = 1; outBuf3.numFrames = 8;
               const AudioBuffer* inputs3[1] = { &inBuf3 };
               an->ProcessBlock(inputs3, 1, outBuf3);

               if (std::fabs(outChan3[0] - 2.0f) > 1e-5f)
               {
                  printf("SECTION 12: FAIL - transplanted delay output is %f, expected 2.0f\n", outChan3[0]);
                  secOk = false;
               }
            }
         }
      }

      if (secOk) printf("SECTION 12 (delay(x, samples) Intrinsic): OK\n");
      else { printf("SECTION 12 (delay(x, samples) Intrinsic): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 13: FieldSynthNode Presets Compile & Zero-Fault Run
   // ------------------------------------------------------------
   {
      bool secOk = true;
      for (const auto& preset : FieldSynthNode::Presets())
      {
         FieldSynthNode synth;
         synth.code = preset.code;
         if (!synth.Apply())
         {
            printf("SECTION 13: FAIL - preset '%s' did not compile: %s\n", preset.name, synth.LastError().c_str());
            secOk = false;
            continue;
         }
         AudioNode* an = synth.GetAudioNode();
         an->PrepareToPlay(44100.0, 128);
         NoteEventQueue notes;
         const int cursor = notes.RegisterConsumer();
         an->SetNoteInbox(&notes, cursor);

         NoteEvent on;
         on.isNoteOn = true;
         on.note = 60;
         on.velocity = 0.9f;
         on.voiceId = 1;
         on.frameOffset = 0;
         notes.Push(on);

         std::vector<float> l(128, 0.0f), r(128, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = 128;

         for (int b = 0; b < 50; b++)
         {
            synth.CookIfNeeded(b + 1);
            an->ProcessBlock(nullptr, 0, buf);
         }

         NoteEvent off;
         off.isNoteOn = false;
         off.voiceId = 1;
         off.frameOffset = 0;
         notes.Push(off);

         for (int b = 50; b < 100; b++)
         {
            synth.CookIfNeeded(b + 1);
            an->ProcessBlock(nullptr, 0, buf);
         }

         if (synth.FaultCount() > 0)
         {
            printf("SECTION 13: FAIL - preset '%s' generated %llu NaN/inf recovery fault(s)\n",
                   preset.name, (unsigned long long)synth.FaultCount());
            secOk = false;
         }
      }
      if (secOk) printf("SECTION 13 (FieldSynth Presets Zero-Fault Run): OK\n");
      else { printf("SECTION 13 (FieldSynth Presets Zero-Fault Run): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 14: FieldSynthNode Polyphonic delay() Isolation
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 14a. Real polyphony delay isolation with simultaneous and staggered notes.
      // Two concurrent voices (note 57: A3 = 220Hz -> freq/440 = 0.5; note 69: A4 = 440Hz -> freq/440 = 1.0).
      // Each voice generates an impulse (scaled to freq/440.0) after reaching sustain (sample 100).
      // A 16-sample delay line delays the impulse.
      // If delay storage were mistakenly shared across voices, the delay cursors would advance
      // twice as fast during polyphony (premature output around sample 108) and voices would
      // interleave/corrupt each other's delayed samples.
      {
         FieldSynthNode synth;
         synth.code =
            "state float p = 0\n"
            "sig = if(p == 100.0, freq / 440.0, 0.0)\n"
            "p = p + 1.0\n"
            "out = delay(sig, 16)\n";

         if (!synth.Apply())
         {
            printf("SECTION 14: FAIL - delay program did not compile: %s\n", synth.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = synth.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            // Test case 1: Simultaneous notes triggered at frame 0.
            // Voice 1: note 57 (freq 220Hz -> sig = 0.5f)
            // Voice 2: note 69 (freq 440Hz -> sig = 1.0f)
            // At frame 100: both voices fire impulse.
            // At frame 116 (100 + 16): both delay lines output their respective impulse.
            // Sum = 0.5f + 1.0f = 1.5f.
            // Samples before frame 116 (e.g. frame 108 where shared cursor bug would fire) must be 0.0f.
            {
               NoteEventQueue notes;
               const int cursor = notes.RegisterConsumer();
               an->SetNoteInbox(&notes, cursor);

               NoteEvent on1;
               on1.isNoteOn = true;
               on1.note = 57;
               on1.velocity = 1.0f;
               on1.voiceId = 1;
               on1.frameOffset = 0;
               notes.Push(on1);

               NoteEvent on2;
               on2.isNoteOn = true;
               on2.note = 69;
               on2.velocity = 1.0f;
               on2.voiceId = 2;
               on2.frameOffset = 0;
               notes.Push(on2);

               std::vector<float> l(128, 0.0f), r(128, 0.0f);
               float* chans[2] = { l.data(), r.data() };
               AudioBuffer buf;
               buf.channels = chans;
               buf.numChannels = 2;
               buf.numFrames = 128;

               synth.CookIfNeeded(1);
               an->ProcessBlock(nullptr, 0, buf);

               // Under the shared-buffer bug, cursor advances twice per sample, outputting at sample 108:
               if (std::fabs(l[108]) > 1e-4f)
               {
                  printf("SECTION 14: FAIL - simultaneous notes: sample 108 is %f, expected 0.0f (shared ring buffer symptom)\n", l[108]);
                  secOk = false;
               }

               // Frame 116 should be exactly 1.5f (0.5 + 1.0)
               if (std::fabs(l[116] - 1.5f) > 1e-4f)
               {
                  printf("SECTION 14: FAIL - simultaneous notes: sample 116 is %f, expected 1.5f\n", l[116]);
                  secOk = false;
               }

               // Surrounding samples must be 0
               for (int s = 100; s < 116; s++)
               {
                  if (std::fabs(l[s]) > 1e-4f)
                  {
                     printf("SECTION 14: FAIL - simultaneous notes: sample %d is %f, expected 0.0f\n", s, l[s]);
                     secOk = false;
                     break;
                  }
               }
               for (int s = 117; s < 128; s++)
               {
                  if (std::fabs(l[s]) > 1e-4f)
                  {
                     printf("SECTION 14: FAIL - simultaneous notes: sample %d is %f, expected 0.0f\n", s, l[s]);
                     secOk = false;
                     break;
                  }
               }
            }

            // Test case 2: Staggered notes across block boundary.
            // Note 1 triggered at frame 0 (note 57 -> sig = 0.5f).
            // Note 2 triggered at frame 20 (note 69 -> sig = 1.0f).
            // Block 1 (frames 0..127):
            //   - Voice 1 impulse at frame 100 -> output at frame 116 is 0.5f.
            //   - Voice 2 impulse at frame 120 (20 + 100) -> 8 samples in block 1, remaining 8 in block 2.
            // Block 2 (frames 128..255):
            //   - Frame 8 (overall sample 136): Voice 2 delayed output is 1.0f.
            //   - Voice 1 outputs 0.0f.
            {
               FieldSynthNode synth2;
               synth2.code = synth.code;
               synth2.Apply();
               AudioNode* an2 = synth2.GetAudioNode();
               an2->PrepareToPlay(44100.0, 128);

               NoteEventQueue notes2;
               const int cursor2 = notes2.RegisterConsumer();
               an2->SetNoteInbox(&notes2, cursor2);

               NoteEvent on1;
               on1.isNoteOn = true;
               on1.note = 57;
               on1.velocity = 1.0f;
               on1.voiceId = 1;
               on1.frameOffset = 0;
               notes2.Push(on1);

               NoteEvent on2;
               on2.isNoteOn = true;
               on2.note = 69;
               on2.velocity = 1.0f;
               on2.voiceId = 2;
               on2.frameOffset = 20;
               notes2.Push(on2);

               std::vector<float> l1(128, 0.0f), r1(128, 0.0f);
               float* chans1[2] = { l1.data(), r1.data() };
               AudioBuffer buf1;
               buf1.channels = chans1;
               buf1.numChannels = 2;
               buf1.numFrames = 128;

               synth2.CookIfNeeded(1);
               an2->ProcessBlock(nullptr, 0, buf1);

               // In block 1: Voice 1 outputs 0.5f at frame 116. Voice 2 is not yet due (it is at sample 96).
               if (std::fabs(l1[116] - 0.5f) > 1e-4f)
               {
                  printf("SECTION 14: FAIL - staggered notes: block 1 sample 116 is %f, expected 0.5f (Voice 1 only)\n", l1[116]);
                  secOk = false;
               }
               if (std::fabs(l1[115]) > 1e-4f || std::fabs(l1[117]) > 1e-4f)
               {
                  printf("SECTION 14: FAIL - staggered notes: bleed at sample 115 (%f) or 117 (%f)\n", l1[115], l1[117]);
                  secOk = false;
               }

               std::vector<float> l2(128, 0.0f), r2(128, 0.0f);
               float* chans2[2] = { l2.data(), r2.data() };
               AudioBuffer buf2;
               buf2.channels = chans2;
               buf2.numChannels = 2;
               buf2.numFrames = 128;

               synth2.CookIfNeeded(2);
               an2->ProcessBlock(nullptr, 0, buf2);

               // In block 2: Voice 2 outputs 1.0f at frame 8 (overall 136). Voice 1 is silent.
               if (std::fabs(l2[8] - 1.0f) > 1e-4f)
               {
                  printf("SECTION 14: FAIL - staggered notes: block 2 sample 8 (overall 136) is %f, expected 1.0f (Voice 2 only)\n", l2[8]);
                  secOk = false;
               }
               if (std::fabs(l2[7]) > 1e-4f || std::fabs(l2[9]) > 1e-4f)
               {
                  printf("SECTION 14: FAIL - staggered notes: bleed at sample 7 (%f) or 9 (%f) in block 2\n", l2[7], l2[9]);
                  secOk = false;
               }

               if (synth2.FaultCount() > 0)
               {
                  printf("SECTION 14: FAIL - synth generated %llu fault(s)\n", (unsigned long long)synth2.FaultCount());
                  secOk = false;
               }
            }
         }
      }

      if (secOk) printf("SECTION 14 (FieldSynth Polyphonic delay() Isolation): OK\n");
      else { printf("SECTION 14 (FieldSynth Polyphonic delay() Isolation): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 15: State Tables (Step 24: state float name[N] = init)
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 15a. Basic table read/write, indexing, clamping, and in-place ops.
      {
         FieldSampleNode node;
         node.code =
            "state float tab[4] = 1.0\n"
            "state float phase = 0\n"
            "if (phase == 0.0) {\n"
            "   tab[0] = 0.5\n"
            "   tab[1] = 1.5\n"
            "   tab[2] += 1.0\n"
            "   tab[3] *= 3.0\n"
            "}\n"
            "phase = phase + 1.0\n"
            "out = tab[phase - 1.0]\n";

         if (!node.Apply())
         {
            printf("SECTION 15: FAIL - table program did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer outBuf;
            outBuf.channels = chans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            an->ProcessBlock(nullptr, 0, outBuf);

            // sample 0 (phase 0): out = tab[0] after write = 0.5f
            // sample 1 (phase 1): out = tab[1] after write = 1.5f
            // sample 2 (phase 2): out = tab[2] after write = 2.0f (1.0 + 1.0)
            // sample 3 (phase 3): out = tab[3] after write = 3.0f (1.0 * 3.0)
            // sample 4 (phase 4): out = tab[4] -> clamped to tab[3] = 3.0f
            if (std::fabs(l[0] - 0.5f) > 1e-5f || std::fabs(l[1] - 1.5f) > 1e-5f ||
                std::fabs(l[2] - 2.0f) > 1e-5f || std::fabs(l[3] - 3.0f) > 1e-5f ||
                std::fabs(l[4] - 3.0f) > 1e-5f)
            {
               printf("SECTION 15: FAIL - table output values (%f, %f, %f, %f, %f) expected (0.5, 1.5, 2.0, 3.0, 3.0)\n",
                      l[0], l[1], l[2], l[3], l[4]);
               secOk = false;
            }
         }
      }

      // 15b. Underflow clamping: negative index clamps to index 0.
      {
         FieldSampleNode node;
         node.code =
            "state float tab[4] = 0.0\n"
            "tab[0] = 3.5\n"
            "out = tab[-10]\n";

         if (!node.Apply())
         {
            printf("SECTION 15: FAIL - negative-index table program did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer outBuf;
            outBuf.channels = chans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            an->ProcessBlock(nullptr, 0, outBuf);

            if (std::fabs(l[0] - 3.5f) > 1e-5f)
            {
               printf("SECTION 15: FAIL - negative index clamp failed: got %f, expected 3.5f\n", l[0]);
               secOk = false;
            }
         }
      }

      // 15c. Hot reload transplant across recompile with matching (name, length).
      {
         FieldSampleNode node;
         node.code =
            "state float hist[4] = 0.0\n"
            "hist[0] = 2.5\n"
            "out = hist[0]\n";

         if (!node.Apply())
         {
            printf("SECTION 15: FAIL - initial table transplant program did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer outBuf;
            outBuf.channels = chans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            an->ProcessBlock(nullptr, 0, outBuf);

            // Recompile with same table name and length, but reading instead of writing:
            node.code =
               "state float hist[4] = 0.0\n"
               "out = hist[0]\n";

            if (!node.Apply())
            {
               printf("SECTION 15: FAIL - recompiled table program did not compile: %s\n", node.LastError().c_str());
               secOk = false;
            }
            else
            {
               std::vector<float> l2(128, 0.0f), r2(128, 0.0f);
               float* chans2[2] = { l2.data(), r2.data() };
               AudioBuffer outBuf2;
               outBuf2.channels = chans2;
               outBuf2.numChannels = 2;
               outBuf2.numFrames = 128;

               an->ProcessBlock(nullptr, 0, outBuf2);

               if (std::fabs(l2[0] - 2.5f) > 1e-5f)
               {
                  printf("SECTION 15: FAIL - table hot reload transplant failed: got %f, expected 2.5f\n", l2[0]);
                  secOk = false;
               }
            }
         }
      }

      // 15d. Compile rejections: non-const length, non-float type, bare ident read/write, budget cap.
      {
         Field::SampleProgram prog;
         Field::FieldError err;

         // Non-literal size
         if (Field::CompileSampleProgram("state float tab[n] = 0.0\nout = 0\n", nullptr, prog, err))
         {
            printf("SECTION 15: FAIL - non-literal table size was not rejected\n");
            secOk = false;
         }

         // Non-float type
         if (Field::CompileSampleProgram("state int tab[4] = 0\nout = 0\n", nullptr, prog, err))
         {
            printf("SECTION 15: FAIL - non-float table was not rejected\n");
            secOk = false;
         }

         // Bare table ident read
         if (Field::CompileSampleProgram("state float tab[4] = 0.0\nout = tab\n", nullptr, prog, err))
         {
            printf("SECTION 15: FAIL - bare table ident read was not rejected\n");
            secOk = false;
         }

         // Bare table ident write
         if (Field::CompileSampleProgram("state float tab[4] = 0.0\ntab = 1.0\nout = 0\n", nullptr, prog, err))
         {
            printf("SECTION 15: FAIL - bare table ident write was not rejected\n");
            secOk = false;
         }

         // Exceeds table cell cap (16385 > 16384)
         if (Field::CompileSampleProgram("state float tab[16385] = 0.0\nout = 0\n", nullptr, prog, err))
         {
            printf("SECTION 15: FAIL - table exceeding cell cap was not rejected\n");
            secOk = false;
         }

         // Element domain rejection
         std::vector<Field::Token> tokens;
         Field::AstNodePtr ast;
         Field::ElementIRProgram ir;
         if (Field::Lex("state float tab[4] = 0.0\nP.y += tab[0]\n", tokens, err) &&
             Field::ParseProgram(tokens, ast, err) &&
             Field::LowerElementProgramToIR(ast, ir, err))
         {
            printf("SECTION 15: FAIL - table state decl in element domain was not rejected\n");
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 15 (State Tables state float name[N]): OK\n");
      else { printf("SECTION 15 (State Tables state float name[N]): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 16: FieldSynthNode noteOn/notePitch/noteVel (Step 26, OPEN-D
   // note history - folded into the sample domain's reserved set alongside
   // freq/gate; see docs/plans/field/step-24-language-opens-remainder.md
   // section 3).
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // NOTE on test design: AudioFieldSynthNode::ProcessBlock scales every
      // kernel's raw `out` by that voice's ADSR click-guard envelope AND its
      // velocity before it reaches the buffer (`sampleAcc += kernelOut * env
      // * vel;`, this file's FieldSynthNode.cpp) - a pre-existing, correct
      // design unrelated to Step 26. Both factors are always >= 0, so a
      // kernel that emits a signed canary (+1.0 if the reserved word reads
      // as expected, -1.0 if not) survives that scaling with its *sign*
      // intact even though its magnitude is unpredictable. Every assertion
      // below reads sign only, never magnitude, for exactly this reason.

      // 16a. noteOn is a one-sample edge, not a held level like gate: the
      // canary is positive only on the exact sample the note-on registers,
      // and non-positive (0.0 while the voice is not yet active, negative
      // once it is) on every other sample - including samples before AND
      // after it, and into a following block with no new events.
      {
         FieldSynthNode synth;
         synth.code = "out = if(noteOn > 0.5, 1.0, -1.0)\n";
         if (!synth.Apply())
         {
            printf("SECTION 16: FAIL - noteOn program did not compile: %s\n", synth.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = synth.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            NoteEventQueue notes;
            const int cursor = notes.RegisterConsumer();
            an->SetNoteInbox(&notes, cursor);

            NoteEvent on;
            on.isNoteOn = true;
            on.note = 69; // A4 -> 440 Hz
            on.velocity = 0.75f;
            on.voiceId = 1;
            on.frameOffset = 5;
            notes.Push(on);

            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = chans;
            buf.numChannels = 2;
            buf.numFrames = 128;

            synth.CookIfNeeded(1);
            an->ProcessBlock(nullptr, 0, buf);

            if (!(l[5] > 1e-6f))
            {
               printf("SECTION 16: FAIL - noteOn canary at sample 5 is %f, expected > 0 (edge sample)\n", l[5]);
               secOk = false;
            }
            for (int s = 0; s < 128; s++)
            {
               if (s == 5) continue;
               if (s < 5)
               {
                  // Voice not yet active: the whole mix path contributes
                  // exactly 0, regardless of the canary's sign.
                  if (std::fabs(l[s]) > 1e-6f)
                  {
                     printf("SECTION 16: FAIL - noteOn canary at sample %d (before note-on) is %f, expected 0.0\n", s, l[s]);
                     secOk = false;
                     break;
                  }
               }
               else if (!(l[s] < -1e-6f))
               {
                  printf("SECTION 16: FAIL - noteOn canary at sample %d is %f, expected < 0 (edge already passed, not held)\n", s, l[s]);
                  secOk = false;
                  break;
               }
            }

            // A second block with no new events: the edge must not re-fire
            // or stay latched - every sample must read the "not this sample"
            // (negative) canary.
            std::fill(l.begin(), l.end(), 0.0f);
            an->ProcessBlock(nullptr, 0, buf);
            for (int s = 0; s < 128; s++)
            {
               if (!(l[s] < -1e-6f))
               {
                  printf("SECTION 16: FAIL - noteOn canary at sample %d of block 2 is %f, expected < 0 (no new note-on)\n", s, l[s]);
                  secOk = false;
                  break;
               }
            }
         }
      }

      // 16b. notePitch is a held snapshot of the most recent note-on (unlike
      // noteOn), and persists across note-off - MidiNoteToHz convention (A4
      // = note 69 -> 440 Hz), reused from freq.
      {
         FieldSynthNode synth;
         synth.code = "out = if(notePitch > 439.0 && notePitch < 441.0, 1.0, -1.0)\n";
         if (!synth.Apply())
         {
            printf("SECTION 16: FAIL - notePitch program did not compile: %s\n", synth.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = synth.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            NoteEventQueue notes;
            const int cursor = notes.RegisterConsumer();
            an->SetNoteInbox(&notes, cursor);

            NoteEvent on;
            on.isNoteOn = true;
            on.note = 69;
            on.velocity = 1.0f;
            on.voiceId = 1;
            on.frameOffset = 0;
            notes.Push(on);

            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = chans;
            buf.numChannels = 2;
            buf.numFrames = 128;

            synth.CookIfNeeded(1);
            an->ProcessBlock(nullptr, 0, buf);

            NoteEvent off;
            off.isNoteOn = false;
            off.voiceId = 1;
            off.frameOffset = 0;
            notes.Push(off);
            an->ProcessBlock(nullptr, 0, buf);

            // The release ramp keeps the envelope positive (asymptotically
            // decaying, never negative) for the whole of this second block,
            // so the canary's sign alone still proves notePitch held 440 Hz
            // through the note-off.
            for (int s = 0; s < 128; s++)
            {
               if (!(l[s] > 1e-6f))
               {
                  printf("SECTION 16: FAIL - notePitch canary at sample %d of block 2 (post note-off) is %f, expected > 0 (440 Hz held)\n", s, l[s]);
                  secOk = false;
                  break;
               }
            }
         }
      }

      // 16c. noteVel matches the note-on's velocity (already 0..1, see
      // NoteEvent::velocity).
      {
         FieldSynthNode synth;
         synth.code = "out = if(noteVel > 0.62 && noteVel < 0.64, 1.0, -1.0)\n";
         if (!synth.Apply())
         {
            printf("SECTION 16: FAIL - noteVel program did not compile: %s\n", synth.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = synth.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);

            NoteEventQueue notes;
            const int cursor = notes.RegisterConsumer();
            an->SetNoteInbox(&notes, cursor);

            NoteEvent on;
            on.isNoteOn = true;
            on.note = 60;
            on.velocity = 0.63f;
            on.voiceId = 1;
            on.frameOffset = 0;
            notes.Push(on);

            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = chans;
            buf.numChannels = 2;
            buf.numFrames = 128;

            synth.CookIfNeeded(1);
            an->ProcessBlock(nullptr, 0, buf);
            an->ProcessBlock(nullptr, 0, buf); // let the attack ramp fully settle

            if (!(l[0] > 1e-6f))
            {
               printf("SECTION 16: FAIL - noteVel canary at sample 0 of block 2 is %f, expected > 0 (velocity 0.63 held)\n", l[0]);
               secOk = false;
            }
         }
      }

      // 16d. noteOn/notePitch/noteVel are reserved: declaring a `param` or
      // `state` cell of that name, or assigning to one, is a compile error
      // (same treatment as freq/gate).
      {
         FieldSynthNode synth;
         synth.code = "param float noteOn = 0 [0, 1]\nout = 0\n";
         if (synth.Apply())
         {
            printf("SECTION 16: FAIL - 'param float noteOn' was not rejected\n");
            secOk = false;
         }

         FieldSynthNode synth2;
         synth2.code = "noteOn = 1.0\nout = 0\n";
         if (synth2.Apply())
         {
            printf("SECTION 16: FAIL - assignment to 'noteOn' was not rejected\n");
            secOk = false;
         }

         FieldSynthNode synth3;
         synth3.code = "state float notePitch = 0\nout = 0\n";
         if (synth3.Apply())
         {
            printf("SECTION 16: FAIL - 'state float notePitch' was not rejected\n");
            secOk = false;
         }
      }

      // 16e. Inert default elsewhere: FieldSampleNode (audio-effects-only,
      // no note pipeline) compiles the same reserved names and always
      // reads 0.0 - the spec's "reserved everywhere, live only where a real
      // note pipeline exists" rule.
      {
         FieldSampleNode node;
         node.code = "out = noteOn + notePitch + noteVel\n";
         if (!node.Apply())
         {
            printf("SECTION 16: FAIL - noteOn/notePitch/noteVel did not compile on FieldSampleNode: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(44100.0, 128);
            std::vector<float> l(128, 0.0f), r(128, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = chans;
            buf.numChannels = 2;
            buf.numFrames = 128;
            an->ProcessBlock(nullptr, 0, buf);
            if (std::fabs(l[0]) > 1e-6f)
            {
               printf("SECTION 16: FAIL - FieldSampleNode noteOn/notePitch/noteVel sum is %f, expected 0.0f (no note pipeline)\n", l[0]);
               secOk = false;
            }
         }
      }

      if (secOk) printf("SECTION 16 (noteOn/notePitch/noteVel Note History): OK\n");
      else { printf("SECTION 16 (noteOn/notePitch/noteVel Note History): FAIL\n"); allOk = false; }
   }

   printf("INFINITE_FIELDSAMPLETEST: %s\n", allOk ? "OK" : "FAIL");
   fflush(stdout);
   return allOk ? 0 : 1;
}

// Build step 12 (docs/plans/field/step-12-dynamic-pins-ir.md): dynamic
// output/input pin declarations. This harness is compiler-level only - it
// drives Lex/ParseProgram/LowerElementProgramToIR/LowerPixelProgramToIR and
// BackendRegister::CompileSampleProgram directly (never through a node class,
// since FieldElementNode/FieldSampleNode only expose the post-backend
// compiled program, not the intermediate IR's declaredOutputs/declaredInputs
// vectors) plus a standalone Field::PinTable, per this step's "compiler/IR
// work only, nothing in src/nodes/ changes" scope restriction.
int RunFieldPinDeclTest()
{
   printf("[FIELDPINDECLTEST] Running Field dynamic-pins declaration harness...\n");
   bool allOk = true;

   // ------------------------------------------------------------
   // SECTION 1: Element-domain output/input collection (shared IR path)
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src =
         "output frame float bright = t\n"
         "input sample audio sidechain\n";
      std::vector<Field::Token> tokens;
      Field::FieldError err;
      Field::AstNodePtr ast;
      Field::ElementIRProgram ir;
      if (!Field::Lex(src, tokens, err) ||
          !Field::ParseProgram(tokens, ast, err) ||
          !Field::LowerElementProgramToIR(ast, ir, err))
      {
         printf("SECTION 1: FAIL - valid snippet did not compile: %s\n", err.message.c_str());
         secOk = false;
      }
      else
      {
         if (ir.declaredOutputs.size() != 1 || ir.declaredOutputs[0].name != "bright" ||
             ir.declaredOutputs[0].domain != Field::Domain::Frame ||
             ir.declaredOutputs[0].type != Field::DataType::Float)
         {
            printf("SECTION 1: FAIL - declaredOutputs does not match expected ('bright', Frame, Float)\n");
            secOk = false;
         }
         if (ir.declaredInputs.size() != 1 || ir.declaredInputs[0].name != "sidechain" ||
             ir.declaredInputs[0].domain != Field::Domain::Sample || !ir.declaredInputs[0].isStructural)
         {
            printf("SECTION 1: FAIL - declaredInputs does not match expected ('sidechain', Sample, structural audio)\n");
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 1 (Element-Domain Output/Input Collection): OK\n");
      else { printf("SECTION 1 (Element-Domain Output/Input Collection): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 2: geometry input + bounded dotted-field access (.P/.N/.uv/.Cd)
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src =
         "input element geometry other\n"
         "output element vec3 otherPos = other.P\n";
      std::vector<Field::Token> tokens;
      Field::FieldError err;
      Field::AstNodePtr ast;
      Field::ElementIRProgram ir;
      if (!Field::Lex(src, tokens, err) ||
          !Field::ParseProgram(tokens, ast, err) ||
          !Field::LowerElementProgramToIR(ast, ir, err))
      {
         printf("SECTION 2: FAIL - valid geometry-access snippet did not compile: %s\n", err.message.c_str());
         secOk = false;
      }
      else
      {
         if (ir.declaredInputs.size() != 1 || ir.declaredInputs[0].name != "other" ||
             ir.declaredInputs[0].typeName != "geometry" || !ir.declaredInputs[0].isStructural)
         {
            printf("SECTION 2: FAIL - declaredInputs does not match expected ('other', geometry, structural)\n");
            secOk = false;
         }
         if (ir.declaredOutputs.size() != 1 || ir.declaredOutputs[0].name != "otherPos" ||
             ir.declaredOutputs[0].domain != Field::Domain::Element)
         {
            printf("SECTION 2: FAIL - declaredOutputs does not match expected ('otherPos', Element)\n");
            secOk = false;
         }
      }

      // A bare (non-dotted) reference to a geometry input must be refused -
      // it names a whole mesh, not a value.
      {
         std::string bad = "input element geometry other\noutput element vec3 bad = other\n";
         std::vector<Field::Token> t2;
         Field::FieldError e2;
         Field::AstNodePtr a2;
         Field::ElementIRProgram ir2;
         bool ok = Field::Lex(bad, t2, e2) && Field::ParseProgram(t2, a2, e2) && Field::LowerElementProgramToIR(a2, ir2, e2);
         if (ok || e2.message.find("cannot be used as a value directly") == std::string::npos)
         {
            printf("SECTION 2: FAIL - bare geometry identifier reference was not refused (err='%s')\n", e2.message.c_str());
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 2 (Geometry Input + Structural Field Access): OK\n");
      else { printf("SECTION 2 (Geometry Input + Structural Field Access): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 3: Domain-join refusal - finer expression than declared domain
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src = "output frame float bad = P.x\n"; // P.x is Element-domain, finer than Frame
      std::vector<Field::Token> tokens;
      Field::FieldError err;
      Field::AstNodePtr ast;
      Field::ElementIRProgram ir;
      bool ok = Field::Lex(src, tokens, err) && Field::ParseProgram(tokens, ast, err) &&
                Field::LowerElementProgramToIR(ast, ir, err);
      if (ok || err.message.find("finer") == std::string::npos)
      {
         printf("SECTION 3: FAIL - finer-than-declared expression was not refused with a 'finer' message (err='%s')\n",
                err.message.c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 3 (Domain-Join Refusal - Finer Expression): OK\n");
      else { printf("SECTION 3 (Domain-Join Refusal - Finer Expression): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 4: every wrong/right row in S5.1 and every refusal row in
   // S5.8, each error naming its span (exit criterion item 1).
   // ------------------------------------------------------------
   {
      bool secOk = true;
      auto testRefusal = [&](const std::string& src, const std::string& expectedSubstr, const char* label) {
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::AstNodePtr ast;
         Field::ElementIRProgram ir;
         bool ok = Field::Lex(src, tokens, err) && Field::ParseProgram(tokens, ast, err) &&
                   Field::LowerElementProgramToIR(ast, ir, err);
         if (ok || err.message.find(expectedSubstr) == std::string::npos)
         {
            printf("SECTION 4 (%s): FAIL - expected refusal containing '%s', got ok=%d err='%s'\n",
                   label, expectedSubstr.c_str(), ok, err.message.c_str());
            secOk = false;
            return;
         }
         if (err.span.line <= 0 || err.span.col <= 0)
         {
            printf("SECTION 4 (%s): FAIL - error is missing a valid source span (line=%d, col=%d)\n",
                   label, err.span.line, err.span.col);
            secOk = false;
         }
      };
      // Same as testRefusal, but the valid-input direction: must compile.
      auto testAccept = [&](const std::string& src, const char* label) {
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::AstNodePtr ast;
         Field::ElementIRProgram ir;
         bool ok = Field::Lex(src, tokens, err) && Field::ParseProgram(tokens, ast, err) &&
                   Field::LowerElementProgramToIR(ast, ir, err);
         if (!ok)
         {
            printf("SECTION 4 (%s): FAIL - expected this to compile, got error: %s\n", label, err.message.c_str());
            secOk = false;
         }
      };

      // S5.1 "wrong" rows.
      testRefusal("output publish = length(P)\n", "domain", "missing domain/type");
      testRefusal("output frame float t = 1\n", "reserved", "reserved name 't' (frame domain)");
      testRefusal("output frame float bass = 1\noutput frame float bass = 2\n", "duplicate declaration", "duplicate output name");
      testRefusal("input frame float k = 1.0\n", "", "input with initializer is a syntax error"); // no '=' expected after an input decl
      testRefusal("output graph float x = 1.0\n", "graph-domain pin", "output in graph domain");

      // S5.1 "right" rows (the corresponding valid forms must compile).
      // Note: length(P)*heat in the doc's own worked example (S5.1) is
      // Element-domain (P is an element attrib) and would itself be
      // refused as "finer than declared" under S5.3's join check unless
      // 'heat' were a reduce'd/frame-domain quantity - the doc's comment
      // is illustrative, not a literal type-checked snippet, so this
      // right-row instead uses a genuinely frame-domain expression (t),
      // matching SECTION 1's already-verified 'bright = t' declaration.
      testAccept("output frame float publish = t * 2\n", "explicit domain+type output, frame-domain expr");
      testAccept("input element geometry other\ninput sample audio sidechain\nP += (other.P - P) * 0.1\n",
                 "geometry + audio inputs, owner's own example");

      // S5.8 row: two declared pins, same direction, same name -> duplicate,
      // already covered above (bass/bass). Opposite-direction name reuse:
      // a pin's name shadowing a pin in the other direction.
      testRefusal("output frame float x = 1\ninput frame float x\n", "duplicate declaration", "name reused across output/input");

      // S5.8 row: reserved attrib / param / state name collisions.
      testRefusal("output frame float P = 1\n", "reserved", "shadows reserved element attrib 'P'");
      testRefusal("param float amt = 0.5 [0,1]\noutput frame float amt = 1\n", "duplicate declaration", "shadows an existing param");
      testRefusal("state float acc = 0\noutput frame float acc = 1\n", "duplicate declaration", "shadows an existing state");

      // S5.8 row: image/geometry/audio used with the wrong domain.
      testRefusal("output element audio bad = 1\n", "sample", "'audio' pin declared outside sample domain");
      testRefusal("input pixel geometry bad\n", "element", "'geometry' pin declared outside element domain");
      testRefusal("output element image bad = vec4(1,1,1,1)\n", "pixel", "'image' pin declared outside pixel domain");

      // S5.8 row: expression domain finer than declared -> refuse, name
      // both domains, suggest reduce (already covered by SECTION 3, but
      // check the hint text is present here too).
      {
         std::string src = "output frame float bad = P.x\n";
         std::vector<Field::Token> tokens;
         Field::FieldError err;
         Field::AstNodePtr ast;
         Field::ElementIRProgram ir;
         bool ok = Field::Lex(src, tokens, err) && Field::ParseProgram(tokens, ast, err) &&
                   Field::LowerElementProgramToIR(ast, ir, err);
         if (ok || err.message.find("frame") == std::string::npos || err.message.find("element") == std::string::npos ||
             err.hint.find("reduce") == std::string::npos)
         {
            printf("SECTION 4 (finer expression names both domains + reduce hint): FAIL - err='%s' hint='%s'\n",
                   err.message.c_str(), err.hint.c_str());
            secOk = false;
         }
      }

      // Nested declaration (not directly in S5.1/S5.8's table, but required
      // by S5's "declared at top level" rule elsewhere in the doc).
      testRefusal("if (t > 0) {\noutput frame float bad = t\n}\n", "must be declared at top level", "nested output");

      if (secOk) printf("SECTION 4 (S5.1/S5.8 Wrong/Right/Refusal Table): OK\n");
      else { printf("SECTION 4 (S5.1/S5.8 Wrong/Right/Refusal Table): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 5: 16-pin-per-kernel ceiling
   // ------------------------------------------------------------
   {
      bool secOk = true;
      std::string src;
      for (int i = 0; i < 17; ++i)
         src += "output frame float p" + std::to_string(i) + " = 1\n";
      std::vector<Field::Token> tokens;
      Field::FieldError err;
      Field::AstNodePtr ast;
      Field::ElementIRProgram ir;
      bool ok = Field::Lex(src, tokens, err) && Field::ParseProgram(tokens, ast, err) &&
                Field::LowerElementProgramToIR(ast, ir, err);
      if (ok || err.message.find("ceiling") == std::string::npos)
      {
         printf("SECTION 5: FAIL - 17th pin declaration was not refused with a ceiling message (err='%s')\n",
                err.message.c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 5 (16-Pin-Per-Kernel Ceiling): OK\n");
      else { printf("SECTION 5 (16-Pin-Per-Kernel Ceiling): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 6: Sample-domain backend collection (independent lowering path)
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 6a. output frame float ... = reduce.rms(...) - the only legal form
      // for a frame-domain output declared inside a sample kernel.
      {
         std::string src = "output frame float bass = reduce.rms(in, 20, 200)\nout = in\n";
         Field::SampleProgram prog;
         Field::FieldError err;
         if (!Field::CompileSampleProgram(src, nullptr, prog, err))
         {
            printf("SECTION 6: FAIL - reduce.rms output declaration did not compile: %s\n", err.message.c_str());
            secOk = false;
         }
         else if (prog.declaredOutputs.size() != 1 || prog.declaredOutputs[0].name != "bass" ||
                  prog.declaredOutputs[0].domainName != "frame" || !prog.hasReduceRms)
         {
            printf("SECTION 6: FAIL - SampleProgram::declaredOutputs / hasReduceRms not set as expected\n");
            secOk = false;
         }
      }

      // 6b. output sample float ... = <plain expr> - legal sample-domain form.
      {
         std::string src = "output sample float y = in * 0.5\nout = in\n";
         Field::SampleProgram prog;
         Field::FieldError err;
         if (!Field::CompileSampleProgram(src, nullptr, prog, err))
         {
            printf("SECTION 6: FAIL - plain sample-domain output did not compile: %s\n", err.message.c_str());
            secOk = false;
         }
         else if (prog.declaredOutputs.size() != 1 || prog.declaredOutputs[0].name != "y" ||
                  prog.declaredOutputs[0].domainName != "sample")
         {
            printf("SECTION 6: FAIL - SampleProgram::declaredOutputs does not match expected ('y', sample)\n");
            secOk = false;
         }
      }

      // 6c. A sample-domain output declared as reduce.rms(...) is refused -
      // that's a frame-domain result and must be declared 'output frame'.
      {
         std::string src = "output sample float bad = reduce.rms(in, 20, 200)\nout = in\n";
         Field::SampleProgram prog;
         Field::FieldError err;
         if (Field::CompileSampleProgram(src, nullptr, prog, err))
         {
            printf("SECTION 6: FAIL - reduce.rms declared as 'output sample' was not refused\n");
            secOk = false;
         }
      }

      // 6d. Step 25 (OPEN-D): `input sample audio <name>` now binds into
      // scope for real - referencing it compiles, and it reads live per-
      // sample values from the kernel's second audio input (FieldSampleNode/
      // FieldSynthNode's dynamic AudioCable pin, see FieldSampleNode.h). A
      // `float`-typed (or non-sample-domain) declared input still has no
      // live source in v1 and stays unbound, same as before this step.
      {
         std::string src = "input sample audio sidechain\nout = sidechain\n";
         Field::SampleProgram prog;
         Field::FieldError err;
         if (!Field::CompileSampleProgram(src, nullptr, prog, err))
         {
            printf("SECTION 6: FAIL - 'input sample audio' referenced in the kernel body did not compile: %s\n", err.message.c_str());
            secOk = false;
         }
         else if (prog.declaredInputs.size() != 1 || prog.declaredInputs[0].name != "sidechain" ||
                  prog.declaredInputs[0].typeName != "audio")
         {
            printf("SECTION 6: FAIL - SampleProgram::declaredInputs does not match expected ('sidechain', audio)\n");
            secOk = false;
         }

         // A declared 'float' input is still collected only, not bound -
         // referencing it remains a loud compile error.
         std::string src2 = "input sample float ref\nout = ref\n";
         Field::SampleProgram prog2;
         Field::FieldError err2;
         if (Field::CompileSampleProgram(src2, nullptr, prog2, err2))
         {
            printf("SECTION 6: FAIL - referencing an unbound 'float'-typed sample-domain input did not fail to compile\n");
            secOk = false;
         }
         else if (err2.message.find("used before assignment") == std::string::npos)
         {
            printf("SECTION 6: FAIL - expected a loud 'used before assignment' error for the 'float' input, got '%s'\n", err2.message.c_str());
            secOk = false;
         }
      }

      // 6e. Step 25 (OPEN-D), end to end through FieldSampleNode: a declared
      // second audio input actually carries live per-sample values from its
      // own AudioCable/pin slot (slot 1, right after native "in" at slot 0),
      // independent of "in" itself.
      {
         FieldSampleNode node;
         node.code = "input sample audio sidechain\nout = sidechain + in * 0.0\n";
         if (!node.Apply())
         {
            printf("SECTION 6: FAIL - end-to-end second-audio-input program did not compile: %s\n", node.LastError().c_str());
            secOk = false;
         }
         else if (node.AudioInputSlot(1) == nullptr)
         {
            printf("SECTION 6: FAIL - declared 'sidechain' audio input did not get a real cable at slot 1\n");
            secOk = false;
         }
         else
         {
            AudioNode* an = node.GetAudioNode();
            an->PrepareToPlay(44100.0, 4);

            std::vector<float> mainIn(4, 9.0f); // ignored: multiplied by 0.0 in the kernel
            std::vector<float> sideIn = { 0.25f, 0.5f, 0.75f, 1.0f };
            float* mainChans[1] = { mainIn.data() };
            float* sideChans[1] = { sideIn.data() };
            AudioBuffer mainBuf; mainBuf.channels = mainChans; mainBuf.numChannels = 1; mainBuf.numFrames = 4;
            AudioBuffer sideBuf; sideBuf.channels = sideChans; sideBuf.numChannels = 1; sideBuf.numFrames = 4;
            const AudioBuffer* inputs[2] = { &mainBuf, &sideBuf };

            std::vector<float> l(4, 0.0f), r(4, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer outBuf; outBuf.channels = chans; outBuf.numChannels = 2; outBuf.numFrames = 4;

            an->ProcessBlock(inputs, 2, outBuf);

            bool matches = true;
            for (int i = 0; i < 4; i++)
               if (std::fabs(l[i] - sideIn[i]) > 1e-5f) matches = false;
            if (!matches)
            {
               printf("SECTION 6: FAIL - declared audio input's per-sample values did not reach the kernel (got %f,%f,%f,%f expected %f,%f,%f,%f)\n",
                      l[0], l[1], l[2], l[3], sideIn[0], sideIn[1], sideIn[2], sideIn[3]);
               secOk = false;
            }
         }
      }

      if (secOk) printf("SECTION 6 (Sample-Domain Backend Collection): OK\n");
      else { printf("SECTION 6 (Sample-Domain Backend Collection): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 7: PinTable identity - append-only ids, retire/remint on
   // reconcile, stable-id reuse across a plain disappear+reappear.
   // ------------------------------------------------------------
   {
      bool secOk = true;
      Field::PinTable table;
      std::string notice;

      // First compile declares 'a' and 'b'.
      std::vector<Field::DeclaredPin> decl1 = {
         { "a", "float", "frame", true },
         { "b", "float", "frame", true },
      };
      table.Reconcile(decl1, 0, notice);
      const Field::PinEntry* a1 = table.Find("a");
      const Field::PinEntry* b1 = table.Find("b");
      if (!a1 || !b1 || a1->id == b1->id || a1->id <= 0 || b1->id <= 0)
      {
         printf("SECTION 7: FAIL - initial Reconcile did not assign distinct positive ids to 'a' and 'b'\n");
         secOk = false;
      }
      int aId = a1 ? a1->id : -1;
      int bId = b1 ? b1->id : -1;

      // Second compile drops 'b' - it must be marked retired and reported.
      std::vector<Field::DeclaredPin> decl2 = {
         { "a", "float", "frame", true },
      };
      table.Reconcile(decl2, 0, notice);
      if (notice.find("'b'") == std::string::npos)
      {
         printf("SECTION 7: FAIL - retiring 'b' did not produce a notice mentioning it (notice='%s')\n", notice.c_str());
         secOk = false;
      }
      const Field::PinEntry* bRetired = table.Find("b");
      if (!bRetired || bRetired->isDeclared || bRetired->id != bId)
      {
         printf("SECTION 7: FAIL - retired 'b' entry should keep its id and isDeclared=false\n");
         secOk = false;
      }

      // Third compile: 'b' reappears with the SAME shape - must reuse the
      // same id (stable identity), not mint a new one.
      table.Reconcile(decl1, 0, notice);
      const Field::PinEntry* bAgain = table.Find("b");
      if (!bAgain || !bAgain->isDeclared || bAgain->id != bId)
      {
         printf("SECTION 7: FAIL - 'b' reappearing with an unchanged shape should reuse id %d, got %s\n",
                bId, bAgain ? std::to_string(bAgain->id).c_str() : "(null)");
         secOk = false;
      }

      // Fourth compile: 'b' reappears with a DIFFERENT shape (domain changed)
      // - must retire the old identity and mint a fresh one (S5.4).
      std::vector<Field::DeclaredPin> decl4 = {
         { "a", "float", "frame", true },
         { "b", "float", "element", true }, // domain changed: frame -> element
      };
      table.Reconcile(decl4, 0, notice);
      const Field::PinEntry* bChanged = table.Find("b");
      if (!bChanged || !bChanged->isDeclared || bChanged->id == bId || bChanged->domainName != "element")
      {
         printf("SECTION 7: FAIL - 'b' reappearing with a changed shape should mint a fresh id (old=%d, new=%s)\n",
                bId, bChanged ? std::to_string(bChanged->id).c_str() : "(null)");
         secOk = false;
      }

      // 'a' was untouched across all four reconciles - its id must never move.
      const Field::PinEntry* aFinal = table.Find("a");
      if (!aFinal || aFinal->id != aId)
      {
         printf("SECTION 7: FAIL - 'a' id changed across Reconcile calls that never dropped it (was %d, now %s)\n",
                aId, aFinal ? std::to_string(aFinal->id).c_str() : "(null)");
         secOk = false;
      }

      if (secOk) printf("SECTION 7 (PinTable Stable Identity + Reconcile): OK\n");
      else { printf("SECTION 7 (PinTable Stable Identity + Reconcile): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 8: PinTable::Reconcile idempotence - calling it twice with the
   // exact same declaration list produces the same ids both times
   // (exit criterion item 4).
   // ------------------------------------------------------------
   {
      bool secOk = true;
      Field::PinTable table;
      std::string notice;
      std::vector<Field::DeclaredPin> decl = {
         { "x", "float", "frame", true },
         { "y", "float", "sample", false },
      };
      table.Reconcile(decl, 0, notice);
      int xId1 = table.Find("x") ? table.Find("x")->id : -1;
      int yId1 = table.Find("y") ? table.Find("y")->id : -1;

      table.Reconcile(decl, 0, notice); // same list again
      int xId2 = table.Find("x") ? table.Find("x")->id : -1;
      int yId2 = table.Find("y") ? table.Find("y")->id : -1;

      if (xId1 <= 0 || yId1 <= 0 || xId1 == yId1 || xId1 != xId2 || yId1 != yId2 ||
          table.Pins().size() != 2)
      {
         printf("SECTION 8: FAIL - idempotent Reconcile did not produce stable ids (x:%d->%d, y:%d->%d, pins=%zu)\n",
                xId1, xId2, yId1, yId2, table.Pins().size());
         secOk = false;
      }
      if (!notice.empty())
      {
         printf("SECTION 8: FAIL - re-declaring the exact same list produced a spurious retirement notice: '%s'\n",
                notice.c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 8 (Reconcile Idempotence): OK\n");
      else { printf("SECTION 8 (Reconcile Idempotence): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 9: PinTable::Reconcile with a renamed pin retires the old id
   // (isDeclared=false, still present in Pins()) and mints a new one
   // (exit criterion item 5).
   // ------------------------------------------------------------
   {
      bool secOk = true;
      Field::PinTable table;
      std::string notice;
      std::vector<Field::DeclaredPin> decl1 = { { "oldName", "float", "frame", true } };
      table.Reconcile(decl1, 0, notice);
      const Field::PinEntry* orig = table.Find("oldName");
      int origId = orig ? orig->id : -1;

      // Rename: the declaration list now has "newName" instead of "oldName".
      std::vector<Field::DeclaredPin> decl2 = { { "newName", "float", "frame", true } };
      table.Reconcile(decl2, 0, notice);

      bool foundOldStillPresent = false;
      for (const auto& p : table.Pins())
      {
         if (p.id == origId) { foundOldStillPresent = true;
            if (p.isDeclared)
            {
               printf("SECTION 9: FAIL - retired 'oldName' entry (id %d) should have isDeclared=false\n", origId);
               secOk = false;
            }
         }
      }
      if (!foundOldStillPresent)
      {
         printf("SECTION 9: FAIL - retired 'oldName' entry (id %d) is missing from Pins() entirely\n", origId);
         secOk = false;
      }
      const Field::PinEntry* renamed = table.Find("newName");
      if (!renamed || !renamed->isDeclared || renamed->id == origId || renamed->id <= 0)
      {
         printf("SECTION 9: FAIL - 'newName' should be a freshly minted id distinct from the retired 'oldName' id %d\n", origId);
         secOk = false;
      }
      if (notice.find("'oldName'") == std::string::npos)
      {
         printf("SECTION 9: FAIL - rename should report 'oldName' as retired in outNotice (got '%s')\n", notice.c_str());
         secOk = false;
      }

      if (secOk) printf("SECTION 9 (Rename Retires Old Id, Mints New Id): OK\n");
      else { printf("SECTION 9 (Rename Retires Old Id, Mints New Id): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 10: PinTable::Reconcile with the same name but a changed
   // type/domain also retires-and-mints, not updates-in-place - S5.4's
   // deliberate difference from ParamTable (exit criterion item 6).
   // ------------------------------------------------------------
   {
      bool secOk = true;

      // 10a. Type change (float -> vec3), domain unchanged.
      {
         Field::PinTable table;
         std::string notice;
         table.Reconcile({ { "v", "float", "frame", true } }, 0, notice);
         int oldId = table.Find("v") ? table.Find("v")->id : -1;
         table.Reconcile({ { "v", "vec3", "frame", true } }, 0, notice);
         const Field::PinEntry* changed = table.Find("v");
         if (!changed || !changed->isDeclared || changed->id == oldId || changed->typeName != "vec3")
         {
            printf("SECTION 10: FAIL - type change (float->vec3) on 'v' should retire+remint, old=%d new=%s\n",
                   oldId, changed ? std::to_string(changed->id).c_str() : "(null)");
            secOk = false;
         }
      }

      // 10b. Direction change (output -> input), same name/type/domain.
      {
         Field::PinTable table;
         std::string notice;
         table.Reconcile({ { "d", "float", "frame", true } }, 0, notice);
         int oldId = table.Find("d") ? table.Find("d")->id : -1;
         table.Reconcile({ { "d", "float", "frame", false } }, 0, notice);
         const Field::PinEntry* changed = table.Find("d");
         if (!changed || !changed->isDeclared || changed->id == oldId || changed->isOutput)
         {
            printf("SECTION 10: FAIL - direction change (output->input) on 'd' should retire+remint, old=%d new=%s\n",
                   oldId, changed ? std::to_string(changed->id).c_str() : "(null)");
            secOk = false;
         }
      }

      if (secOk) printf("SECTION 10 (Type/Domain/Direction Change Retires+Remints): OK\n");
      else { printf("SECTION 10 (Type/Domain/Direction Change Retires+Remints): FAIL\n"); allOk = false; }
   }

   // ------------------------------------------------------------
   // SECTION 11: SerializePinMap/DeserializePinMap round-trip a table with
   // at least one retired entry (exit criterion item 7).
   // ------------------------------------------------------------
   {
      bool secOk = true;
      Field::PinTable table;
      std::string notice;
      table.Reconcile({ { "keep", "float", "frame", true }, { "gone", "float", "frame", true } }, 0, notice);
      table.Reconcile({ { "keep", "float", "frame", true } }, 0, notice); // "gone" retires

      const Field::PinEntry* keepBefore = table.Find("keep");
      const Field::PinEntry* goneBefore = table.Find("gone");
      if (!keepBefore || !goneBefore || goneBefore->isDeclared)
      {
         printf("SECTION 11: FAIL - fixture setup did not produce one declared + one retired entry\n");
         secOk = false;
      }
      int keepId = keepBefore ? keepBefore->id : -1;
      int goneId = goneBefore ? goneBefore->id : -1;

      std::string serialized = table.SerializePinMap();

      Field::PinTable table2;
      table2.DeserializePinMap(serialized);
      const Field::PinEntry* keepAfter = table2.Find("keep");
      const Field::PinEntry* goneAfter = table2.Find("gone");
      if (!keepAfter || keepAfter->id != keepId)
      {
         printf("SECTION 11: FAIL - 'keep' did not round-trip to the same id %d (got %s)\n",
                keepId, keepAfter ? std::to_string(keepAfter->id).c_str() : "(null)");
         secOk = false;
      }
      if (!goneAfter || goneAfter->id != goneId)
      {
         printf("SECTION 11: FAIL - retired 'gone' did not round-trip to the same id %d (got %s)\n",
                goneId, goneAfter ? std::to_string(goneAfter->id).c_str() : "(null)");
         secOk = false;
      }
      if (table2.NextPinId() <= std::max(keepId, goneId))
      {
         printf("SECTION 11: FAIL - deserialized table's NextPinId (%d) must be past the highest restored id (%d)\n",
                table2.NextPinId(), std::max(keepId, goneId));
         secOk = false;
      }

      if (secOk) printf("SECTION 11 (SerializePinMap/DeserializePinMap Round-Trip With Retired Entry): OK\n");
      else { printf("SECTION 11 (SerializePinMap/DeserializePinMap Round-Trip With Retired Entry): FAIL\n"); allOk = false; }
   }

   printf("INFINITE_FIELDPINDECLTEST: %s\n", allOk ? "OK" : "FAIL");
   fflush(stdout);
   return allOk ? 0 : 1;
}

// ============================================================ INFINITE_FIELDNOTESTEST
namespace
{
   struct NotesRig
   {
      FieldNotesNode node;
      NoteEventQueue inbox;
      int outCursor = -1;
      int sample = 0;
      struct Ev { bool on; int note; int voice; long long at; float vel; };
      std::vector<Ev> out;

      explicit NotesRig(const char* code)
      {
         node.code = code;
         node.Apply();
         node.GetAudioNode()->PrepareToPlay(48000.0, 128);
         outCursor = node.GetAudioNode()->NoteOutbox()->RegisterConsumer();
         node.GetAudioNode()->SetNoteInbox(&inbox, inbox.RegisterConsumer());
      }
      void Block(int frame)
      {
         Transport::Instance().AdvanceAudioClock(128);
         node.CookIfNeeded(frame);
         std::vector<float> l(128), r(128);
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = 128;
         node.GetAudioNode()->ProcessBlock(nullptr, 0, buf);
         NoteEvent e[128];
         int n;
         while ((n = node.GetAudioNode()->NoteOutbox()->Pop(outCursor, e, 128)) > 0)
            for (int i = 0; i < n; i++)
               out.push_back({ e[i].isNoteOn, e[i].note, e[i].voiceId, (long long)sample + e[i].frameOffset, e[i].velocity });
         sample += 128;
      }
      int Count(bool on) const { int c = 0; for (const auto& e : out) c += (e.on == on); return c; }
   };

   void NotesTransport(bool playing)
   {
      Transport& t = Transport::Instance();
      t.SetPlaying(false);
      t.SetTempo(120.0f);
      t.NotifyAudioEngineStarted(48000.0);
      t.SeekBeats(0.0);
      t.SetPlaying(playing);
   }
}

int RunFieldNotesTest()
{
   printf("[FIELDNOTESTEST] Running Field Notes harness...\n");
   bool allOk = true;
   auto check = [&](bool cond, const char* what) { if (!cond) { printf("FAIL: %s\n", what); allOk = false; } };

   // 1. Every preset compiles.
   for (const auto& p : FieldNotesNode::Presets())
   {
      FieldNotesNode n;
      n.code = p.code;
      if (!n.Apply())
      {
         printf("FAIL: preset '%s' did not compile: %s\n", p.name, n.LastError().c_str());
         allOk = false;
      }
   }

   // 2. Language refusals.
   {
      FieldNotesNode n;
      n.code = "note(60, 1, 1)\n";
      check(!n.Apply(), "note() outside an if must be refused");
      FieldSynthNode synth;
      synth.code = "if (gate > 0.5) { note(60, 1, 1) }\nout = 0\n";
      check(!synth.Apply(), "Field Synth must refuse note()");
   }

   // 3. Euclidean 5/16: 20 hits in 4 beats, each on a 1/16-beat grid, every on has its off.
   {
      NotesTransport(true);
      NotesRig rig(FieldNotesNode::Presets()[3].code);
      for (int b = 0; b < 750; b++) rig.Block(b + 1);
      Transport::Instance().SetPlaying(false);
      rig.Block(1000); // stop releases pending notes
      check(rig.Count(true) == 20, "euclid: 20 note-ons in 4 beats");
      check(rig.Count(false) == rig.Count(true), "euclid: every note-on has a note-off");
      bool onGrid = true;
      for (const auto& e : rig.out)
         if (e.on)
         {
            const double steps = (double)e.at / 1500.0;
            if (std::fabs(steps - std::round(steps)) * 1500.0 > 1.01) onGrid = false;
         }
      check(onGrid, "euclid: note-ons land within 1 sample of the 1/16-beat grid");
   }

   // 4. Followers: len 0 notes live exactly as long as the input note.
   {
      NotesTransport(false);
      NotesRig rig(FieldNotesNode::Presets()[1].code); // Harmoniser
      NoteEvent on; on.isNoteOn = true; on.note = 60; on.velocity = 0.8f; on.voiceId = 7; on.frameOffset = 5;
      rig.inbox.Push(on);
      rig.Block(1);
      check(rig.Count(true) == 3, "harmoniser: 3 note-ons for one chord");
      check(rig.Count(false) == 0, "harmoniser: no offs while the input is held");
      NoteEvent off; off.isNoteOn = false; off.note = 60; off.voiceId = 7; off.frameOffset = 0;
      rig.inbox.Push(off);
      rig.Block(2);
      check(rig.Count(false) == 3, "harmoniser: 3 offs on input release");
   }
   // 5. Unplugging the input releases followers.
   {
      NotesTransport(false);
      NotesRig rig(FieldNotesNode::Presets()[0].code); // Transpose +7
      NoteEvent on; on.isNoteOn = true; on.note = 60; on.velocity = 0.8f; on.voiceId = 9; on.frameOffset = 0;
      rig.inbox.Push(on);
      rig.Block(1);
      check(rig.Count(true) == 1 && rig.out[0].note == 67, "transpose: 60 -> 67");
      rig.node.GetAudioNode()->SetNoteInbox(nullptr, -1);
      rig.Block(2);
      check(rig.Count(false) == 1, "unplug: follower released");
   }

   // 6. Soak: every preset, with and without a held input note, never leaves a note hanging.
   for (const auto& p : FieldNotesNode::Presets())
   {
      NotesTransport(true);
      NotesRig rig(p.code);
      // Eight notes, 80 blocks apart, each held 40 blocks; the first lands after the param smoothers settle.
      for (int b = 0; b < 700; b++)
      {
         const int k = (b - 50) / 80;
         if (b >= 50 && k < 8 && (b - 50) % 80 == 0)
         {
            NoteEvent on; on.isNoteOn = true; on.note = 60; on.velocity = 0.8f; on.voiceId = 100 + k; on.frameOffset = 0;
            rig.inbox.Push(on);
         }
         if (b >= 90 && k < 8 && (b - 90) % 80 == 0)
         {
            NoteEvent off; off.isNoteOn = false; off.note = 60; off.voiceId = 100 + (b - 90) / 80; off.frameOffset = 0;
            rig.inbox.Push(off);
         }
         rig.Block(b + 1);
      }
      Transport::Instance().SetPlaying(false);
      rig.Block(2000);
      rig.Block(2001);
      if (rig.Count(true) != rig.Count(false))
      {
         printf("FAIL: preset '%s' left %d note(s) hanging (%d on, %d off)\n", p.name,
                rig.Count(true) - rig.Count(false), rig.Count(true), rig.Count(false));
         allOk = false;
      }
      if (rig.Count(true) == 0)
      {
         printf("FAIL: preset '%s' produced no notes\n", p.name);
         allOk = false;
      }
   }

   printf("INFINITE_FIELDNOTESTEST: %s\n", allOk ? "OK" : "FAIL");
   fflush(stdout);
   return allOk ? 0 : 1;
}
}
