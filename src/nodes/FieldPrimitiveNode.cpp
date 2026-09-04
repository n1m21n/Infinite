#include "FieldPrimitiveNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>

const std::vector<FieldPrimitiveNode::Preset>& FieldPrimitiveNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Circle",
        "param float radius = 1.0 [0.1, 5.0]\n"
        "param float speed = 1.0 [0.0, 5.0]\n"
        "u = i / count\n"
        "angle = u * 6.2831853 + t * speed\n"
        "P = vec3(cos(angle) * radius, sin(angle) * radius, 0.0)\n"
        "Cd = vec3(0.5 + 0.5 * cos(angle), 0.5 + 0.5 * sin(angle), 0.8)\n"
        "publish = sin(t * speed)\n" },
      { "Spiral",
        "param float turns = 4.0 [1.0, 20.0]\n"
        "param float maxRadius = 1.5 [0.2, 5.0]\n"
        "param float height = 1.0 [0.0, 4.0]\n"
        "param float speed = 1.5 [0.0, 8.0]\n"
        "u = i / count\n"
        "angle = u * turns * 6.2831853 + t * speed\n"
        "r = u * maxRadius\n"
        "P = vec3(cos(angle) * r, (u - 0.5) * height, sin(angle) * r)\n"
        "Cd = vec3(u, 0.7, 1.0 - u)\n"
        "publish = sin(t * speed)\n" },
      { "Grid Lattice",
        "param float size = 2.0 [0.5, 10.0]\n"
        "side = max(1.0, floor(sqrt(count)))\n"
        "gx = mod(i, side)\n"
        "gy = floor(i / side)\n"
        "ux = gx / max(1.0, side - 1.0)\n"
        "uy = gy / max(1.0, side - 1.0)\n"
        "P = vec3((ux - 0.5) * size, 0.0, (uy - 0.5) * size)\n"
        "Cd = vec3(ux, 0.4, uy)\n"
        "publish = sin(t)\n" },
      { "Fibonacci Sphere",
        "param float radius = 1.2 [0.2, 5.0]\n"
        "param float speed = 0.5 [0.0, 3.0]\n"
        "phi = 2.39996323\n"
        "y = 1.0 - (i / max(1.0, count - 1.0)) * 2.0\n"
        "rAtY = sqrt(max(0.0, 1.0 - y * y))\n"
        "theta = phi * i + t * speed\n"
        "P = vec3(cos(theta) * rAtY * radius, y * radius, sin(theta) * rAtY * radius)\n"
        "N = normalize(P)\n"
        "Cd = N * 0.5 + 0.5\n"
        "publish = sin(t * speed)\n" },
      { "Helix",
        "param float radius = 0.8 [0.1, 3.0]\n"
        "param float pitch = 2.0 [0.2, 8.0]\n"
        "param float turns = 3.0 [1.0, 15.0]\n"
        "param float speed = 2.0 [0.0, 10.0]\n"
        "u = i / count\n"
        "a = u * turns * 6.2831853 + t * speed\n"
        "P = vec3(cos(a) * radius, (u - 0.5) * pitch, sin(a) * radius)\n"
        "Cd = vec3(0.3, 0.5 + 0.5 * sin(a), 0.9)\n"
        "publish = sin(t * speed)\n" },
      { "Torus Knot",
        "param float p = 2.0 [1.0, 10.0]\n"
        "param float q = 3.0 [1.0, 10.0]\n"
        "param float rMajor = 1.2 [0.2, 4.0]\n"
        "param float rMinor = 0.4 [0.05, 2.0]\n"
        "param float speed = 1.0 [0.0, 5.0]\n"
        "phi = (i / count) * 6.2831853 + t * speed\n"
        "r = rMajor + rMinor * cos(q * phi)\n"
        "P = vec3(r * cos(p * phi), rMinor * sin(q * phi), r * sin(p * phi))\n"
        "Cd = vec3(0.5 + 0.5 * cos(p * phi), 0.5 + 0.5 * sin(q * phi), 0.8)\n"
        "publish = sin(t * speed)\n" }
   };
   return kPresets;
}

const std::vector<std::string>& FieldPrimitiveNode::PresetNames()
{
   static std::vector<std::string> kNames;
   if (kNames.empty())
   {
      for (const auto& p : Presets())
         kNames.push_back(p.name);
   }
   return kNames;
}

void FieldPrimitiveNode::LoadPreset(int index)
{
   const auto& presets = Presets();
   if (index >= 0 && index < (int)presets.size())
   {
      presetIndex = index;
      code = presets[index].code;
      Apply();
   }
}

Field::DeviceFile FieldPrimitiveNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "primitive";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   device.nodeSettings["maxElements"] = (double)maxElements;
   device.nodeSettings["count"] = (double)count;
   return device;
}

void FieldPrimitiveNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itMax = device.nodeSettings.find("maxElements");
   if (itMax != device.nodeSettings.end())
      maxElements = (int)itMax->second;
   auto itCount = device.nodeSettings.find("count");
   if (itCount != device.nodeSettings.end())
      count = (int)itCount->second;
   Apply();
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}

FieldPrimitiveNode::FieldPrimitiveNode()
{
   mPublishOutput.owner = this;
   // Start with Circle preset code
   const auto& presets = Presets();
   if (!presets.empty())
      code = presets[0].code;
   else
      code = "u = i / count\nangle = u * 6.2831853\nP = vec3(cos(angle), sin(angle), 0.0)\n";
   count = 256;
   Apply();
}

bool FieldPrimitiveNode::Apply()
{
   pinRefusal.clear();
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

   {
      std::vector<Field::DeclaredPin> declOut, declIn;
      for (const auto& d : irProgram.declaredOutputs)
         declOut.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), true });
      for (const auto& d : irProgram.declaredInputs)
         declIn.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), false });

      std::string pinNotice, pinRefusal;
      bool outOk = Field::ReconcileFieldPins(mOutputPins, declOut, mNodeIndex, NativeOutputCount(), pinNotice, pinRefusal);
      // native input count is 0 for FieldPrimitiveNode (pure generator)
      bool inOk = outOk && Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, /*nativeCount=*/0, pinNotice, pinRefusal);
      if (!outOk || !inOk)
      {
         mLastError = pinRefusal;
         this->pinRefusal = pinRefusal;
         return false;
      }
      if (!pinNotice.empty())
         mNotice = pinNotice;
   }

   mParamTable.Reconcile(prog->declaredParams, mNodeIndex, mNotice);

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

   for (const auto& decl : prog->declaredAttribs)
   {
      mStore.DeclareAttrib(decl.first, decl.second);
   }

   mProgram = prog;
   mLastError.clear();
   mMeshRevision = NextMeshRevision();
   mLastBuiltCount = 0;
   mLastEvalT = -999999.0f;
   return true;
}

void FieldPrimitiveNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;

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

   int n = std::max(1, count);
   size_t boundCount = std::min((size_t)n, (size_t)std::max(1, maxElements));
   mWasTruncated = ((size_t)n > boundCount);
   mActualElementCount = (int)boundCount;

   double t = Transport::Instance().Seconds();

   std::map<std::string, float> paramValues = mParamTable.ValueMap();
   size_t paramHash = 0;
   for (const auto& kv : paramValues)
   {
      paramHash ^= std::hash<std::string>{}(kv.first) + 0x9e3779b9 + (paramHash << 6) + (paramHash >> 2);
      paramHash ^= std::hash<float>{}(kv.second) + 0x9e3779b9 + (paramHash << 6) + (paramHash >> 2);
   }

   bool needRebuild = ((unsigned long long)boundCount != mLastBuiltCount) ||
                      (mProgram && mProgram->isTimeDependent && (float)t != mLastEvalT) ||
                      (mOutMesh.vertices.empty()) ||
                      (!mState.Cells().empty()) ||
                      (paramHash != mLastParamHash);

   if (!needRebuild)
      return;

   mLastParamHash = paramHash;

   // Synthesize degenerate base scaffold of size boundCount
   Mesh baseMesh;
   baseMesh.vertices.resize(boundCount);
   for (size_t i = 0; i < boundCount; ++i)
   {
      float u = (boundCount > 1) ? ((float)i / (float)(boundCount - 1)) : 0.0f;
      baseMesh.vertices[i].px = (u - 0.5f) * 2.0f;
      baseMesh.vertices[i].py = 0.0f;
      baseMesh.vertices[i].pz = 0.0f;
      baseMesh.vertices[i].nx = 0.0f;
      baseMesh.vertices[i].ny = 1.0f;
      baseMesh.vertices[i].nz = 0.0f;
      baseMesh.vertices[i].u = u;
      baseMesh.vertices[i].v = 0.0f;
   }

   if (!mProgram)
   {
      mOutMesh = baseMesh;
      mMeshRevision = NextMeshRevision();
      mLastBuiltCount = (unsigned long long)boundCount;
      return;
   }

   if (mState.ElementStorageCount() != boundCount)
   {
      mState.Allocate(Field::Domain::Element, boundCount);
      mState.FormatCost(mCostReadout, sizeof(mCostReadout), (int)boundCount);
   }

   // 1. Gather (AoS -> SoA)
   mStore.FromMesh(baseMesh, boundCount);

   // 2. Execute Element VM
   Field::ExecutionEnv env;
   env.t = t;
   env.dt = (mLastEvalT > -900000.0f) ? (t - mLastEvalT) : (1.0 / 60.0);
   env.frame = (double)frameId;
   env.params = &paramValues;
   env.state = &mState;
   std::string vmErr;
   mVM.Execute(*mProgram, mStore, env, vmErr);

   // 3. Scatter (SoA -> AoS)
   mStore.ToMesh(mOutMesh, mProgram->WriteMask(), baseMesh, boundCount);

   mMeshRevision = NextMeshRevision();
   mLastBuiltCount = (unsigned long long)boundCount;
   mLastEvalT = (float)t;
}
