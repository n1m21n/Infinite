#include "FieldElementNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"

#include <algorithm>

const std::vector<FieldElementNode::Preset>& FieldElementNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Wave Displace", "P.y += sin(P.x * 4.0 + t * 2.0) * 0.25\n" },
      { "Color by Normal", "Cd = N * 0.5 + 0.5\n" },
      { "Twist", "a = P.y * 2.0 + t\nP.x = P.x * cos(a) - P.z * sin(a)\nP.z = P.x * sin(a) + P.z * cos(a)\n" },
      { "Noise Ripple", "n = rand(0, 1, 2.0, 0)\nP += N * (n - 0.5) * 0.2\n" },
      { "Attrib Ramp", "attrib float heat = 0\nheat = (P.y + 1.0) * 0.5\nCd = vec3(heat, 0.2, 1.0 - heat)\n" }
   };
   return kPresets;
}

const std::vector<std::string>& FieldElementNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
   {
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   }
   return kNames;
}

void FieldElementNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      code = presets[index].code;
      Apply();
   }
}

FieldElementNode::FieldElementNode()
{
   mPublishOutput.owner = this;
   code = "P.y += sin(P.x * 2.0 + t) * 0.2\n";
   Apply();
}

bool FieldElementNode::Apply()
{
   std::vector<Field::Token> tokens;
   Field::FieldError err;

   if (!Field::Lex(code, tokens, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   Field::AstNodePtr astProgram;
   if (!Field::ParseProgram(tokens, astProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   Field::ElementIRProgram irProgram;
   if (!Field::LowerElementProgramToIR(astProgram, irProgram, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   auto prog = std::make_shared<Field::ElementProgram>();
   if (!Field::EmitElementBytecode(irProgram, *prog, err))
   {
      mLastError = err.message + " at line " + std::to_string(err.span.line) + ", col " + std::to_string(err.span.col);
      return false;
   }

   // Reconcile param table with newly declared params
   mParamTable.Reconcile(prog->declaredParams, mNodeIndex, mNotice);

   // Reconcile state cells (§5.5)
   Field::FieldState newState;
   for (const auto& ds : prog->declaredStates)
   {
      newState.DeclareCell(ds.name, ds.typeName, ds.type, ds.lanes, ds.initialValues, ds.domain);
   }
   size_t allocCount = (mActualElementCount > 0) ? (size_t)mActualElementCount : (size_t)std::max(1, maxElements);
   newState.Allocate(Field::Domain::Element, allocCount);
   newState.Transplant(mState);
   mState = std::move(newState);
   mState.FormatCost(mCostReadout, sizeof(mCostReadout), (int)allocCount);

   // Declare user attributes on store
   for (const auto& decl : prog->declaredAttribs)
   {
      mStore.DeclareAttrib(decl.first, decl.second);
   }

   mProgram = prog;
   mLastError.clear();
   mMeshRevision = NextMeshRevision();
   mLastUpstreamRevision = 0;
   mLastEvalT = -999999.0f;
   return true;
}

void FieldElementNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   // Check transport reset epoch (§5.3)
   unsigned long long currentEpoch = Transport::Instance().ResetEpoch();
   if (currentEpoch != mLastResetEpoch)
   {
      mState.ResetAll();
      mLastResetEpoch = currentEpoch;
   }

   if (!mProgram && mLastError.empty())
   {
      Apply();
   }

   // With nothing wired into `input`, act as a generator instead of a
   // modifier - same shape as DistributePointsInGridNode's no-input point
   // generation, just driven by the Field kernel instead of a grid formula.
   // Points start spread along X so a kernel like "Wave Displace" produces
   // something visible with zero extra code.
   Mesh generatedMesh;
   const Mesh* srcMesh;
   unsigned long long upRev;
   if (input)
   {
      srcMesh = &input->GetMesh();
      if (srcMesh->vertices.empty())
      {
         if (!mOutMesh.vertices.empty())
         {
            mOutMesh.vertices.clear();
            mOutMesh.indices.clear();
            mOutMesh.faceMask.clear();
            mOutMesh.selectionGroup.clear();
            mOutMesh.vertexColor.clear();
            mMeshRevision = NextMeshRevision();
         }
         return;
      }
      upRev = input->MeshRevision();
   }
   else
   {
      int n = std::max(1, generateCount);
      generatedMesh.vertices.resize((size_t)n);
      for (int i = 0; i < n; ++i)
      {
         float u = (n > 1) ? ((float)i / (float)(n - 1)) : 0.0f;
         generatedMesh.vertices[(size_t)i].px = (u - 0.5f) * 2.0f;
      }
      srcMesh = &generatedMesh;
      // Not a real mesh revision counter - only ever compared for equality
      // against mLastUpstreamRevision to detect a generateCount change.
      upRev = (unsigned long long)n;
   }
   const Mesh& inMesh = *srcMesh;

   double t = Transport::Instance().Seconds();

   bool needRebuild = (upRev != mLastUpstreamRevision) ||
                      (mProgram && mProgram->isTimeDependent && (float)t != mLastEvalT) ||
                      (mOutMesh.vertices.empty()) ||
                      (!mState.Cells().empty());

   if (!needRebuild)
      return;

   if (!mProgram)
   {
      // Pass through untouched if no working program
      mOutMesh = inMesh;
      mMeshRevision = upRev;
      mLastUpstreamRevision = upRev;
      return;
   }

   size_t inCount = inMesh.vertices.size();
   size_t boundCount = std::min(inCount, (size_t)std::max(1, maxElements));
   mWasTruncated = (inCount > boundCount);
   mActualElementCount = (int)boundCount;

   if (mState.ElementStorageCount() != boundCount)
   {
      mState.Allocate(Field::Domain::Element, boundCount);
      mState.FormatCost(mCostReadout, sizeof(mCostReadout), (int)boundCount);
   }

   // 1. Gather (AoS -> SoA)
   mStore.FromMesh(inMesh, boundCount);

   // 2. Execute Element VM
   Field::ExecutionEnv env;
   env.t = t;
   env.dt = (mLastEvalT > -900000.0f) ? (t - mLastEvalT) : (1.0 / 60.0);
   env.frame = (double)frameId;
   std::map<std::string, float> paramValues = mParamTable.ValueMap();
   env.params = &paramValues;
   env.state = &mState;
   std::string vmErr;
   mVM.Execute(*mProgram, mStore, env, vmErr);

   // 3. Scatter (SoA -> AoS)
   mStore.ToMesh(mOutMesh, mProgram->WriteMask(), inMesh, boundCount);

   mMeshRevision = NextMeshRevision();
   mLastUpstreamRevision = upRev;
   mLastEvalT = (float)t;
}
