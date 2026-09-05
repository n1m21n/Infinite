#include "FieldElementNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"

#include <algorithm>

const std::vector<FieldElementNode::Preset>& FieldElementNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Wave Displace",
        "param float amp = 0.3 [0.0, 2.0]\n"
        "param float freq = 3.5 [0.5, 10.0]\n"
        "param float speed = 2.0 [0.0, 8.0]\n"
        "P.y += sin(P.x * freq + t * speed) * amp\n"
        "publish = sin(t * speed)\n" },
      { "Twist Modifier",
        "param float twist = 2.5 [0.0, 8.0]\n"
        "a = P.y * twist\n"
        "px = P.x\n"
        "pz = P.z\n"
        "P.x = px * cos(a) - pz * sin(a)\n"
        "P.z = px * sin(a) + pz * cos(a)\n"
        "publish = sin(t)\n" },
      { "Radial Ripple",
        "param float freq = 8.0 [1.0, 25.0]\n"
        "param float amp = 0.25 [0.0, 1.5]\n"
        "param float speed = 3.0 [0.0, 10.0]\n"
        "d = length(vec2(P.x, P.z))\n"
        "P.y += sin(d * freq - t * speed) * amp / (1.0 + d)\n"
        "publish = sin(t * speed)\n" },
      { "Spherical Bulge",
        "param float radius = 1.0 [0.2, 3.0]\n"
        "param float strength = 0.5 [-1.5, 1.5]\n"
        "d = length(P)\n"
        "factor = max(0.0, 1.0 - d / radius)\n"
        "P += N * (factor * factor * strength)\n"
        "publish = sin(t)\n" },
      { "Normal Colorizer",
        "param float blend = 1.0 [0.0, 1.0]\n"
        "normCol = N * 0.5 + 0.5\n"
        "Cd = mix(Cd, normCol, blend)\n"
        "publish = sin(t)\n" },
      { "Noise Ripple", "n = rand(0, 1, 2.0, 0)\nP += N * (n - 0.5) * 0.2\npublish = sin(t)\n" },
      { "Attrib Ramp", "attrib float heat = 0\nheat = (P.y + 1.0) * 0.5\nCd = vec3(heat, 0.2, 1.0 - heat)\npublish = sin(t)\n" },
      { "Bass Ribbon Mirror",
        "param float amp = 0.8 [0.0, 3.0]\n"
        "param float wave = 4.0 [0.5, 16.0]\n"
        "param float speed = 3.0 [0.5, 10.0]\n"
        "disp = sin(P.x * wave + t * speed) * amp * 0.25\n"
        "P.y += disp\n"
        "Cd = vec3(0.5 + 0.5 * sin(P.x * 2.0 + t), 0.3, 0.9)\n"
        "output frame float glow = abs(sin(t * speed))\n"
        "publish = abs(sin(t * speed))\n" },
      { "Boundary Chime Sensor",
        "param float threshold = 0.3 [0.0, 1.0]\n"
        "param float freq = 2.0 [0.1, 8.0]\n"
        "P.y += sin(P.x * 4.0 + t * freq) * 0.1\n"
        "hit = if(P.y > threshold, 1.0, 0.0)\n"
        "Cd = if(hit > 0.5, vec3(1.0, 0.2, 0.2), vec3(0.2, 0.6, 1.0))\n"
        "output frame float chime = reduce.max(hit)\n"
        "publish = reduce.max(hit)\n" },
      // Build step 23 (OPEN-B). Both of these are simulations: the shape they
      // settle into is nowhere in the text, it is what the rule does after a
      // few hundred cooks. They keep their positions in `state` rather than in
      // P because the store is refilled from the incoming mesh every cook, so a
      // `P +=` accumulates nothing on its own.
      { "Verlet Rope",
        "param float restDist = 0.06 [0.01, 0.4]\n"
        "param float stiff = 0.7 [0.05, 1.0]\n"
        "param float gravity = 4.0 [0.0, 20.0]\n"
        "param float sway = 0.8 [0.0, 3.0]\n"
        "state vec3 Q = vec3(0, 0, 0)\n"
        "state vec3 prevQ = vec3(0, 0, 0)\n"
        "first = 1.0 - step(0.5, age)\n"
        "grow = 1.0 - first\n"
        "Q = mix(Q, P, first)\n"
        "prevQ = mix(prevQ, P, first)\n"
        "cur = Q\n"
        "vel = (cur - prevQ) * 0.985\n"
        "Q = cur + vel\n"
        "Q.y -= gravity * dt * dt\n"
        "prevQ = cur\n"
        "isRoot = if(i < 0.5, 1.0, 0.0)\n"
        "nbr = Q.at(i - 1)\n"
        "delta = Q - nbr\n"
        "len = max(length(delta), 0.0001)\n"
        "err = len - restDist\n"
        "Q -= (delta / len) * err * stiff * grow * (1.0 - isRoot)\n"
        "root = P + vec3(sin(t * 1.3) * sway * 0.2, 0.0, cos(t * 0.9) * sway * 0.2)\n"
        "Q = mix(Q, root, isRoot)\n"
        "prevQ = mix(prevQ, root, isRoot)\n"
        "P = Q\n"
        "Cd = vec3(0.9, 0.45 + 0.4 * isRoot, 0.25)\n"
        "publish = reduce.max(len)\n" },
      { "Buckling Ribbon",
        "param float springK = 0.25 [0.02, 0.8]\n"
        "param float repel = 0.03 [0.0, 0.15]\n"
        "param float speed = 1.5 [0.0, 6.0]\n"
        "param float detail = 0.35 [0.05, 1.5]\n"
        "state vec3 G = vec3(0, 0, 0)\n"
        "first = 1.0 - step(0.5, age)\n"
        "grow = 1.0 - first\n"
        "G = mix(G, P, first)\n"
        "a = G.at(i - 1)\n"
        "b = G.at(i + 1)\n"
        "mid = (a + b) * 0.5\n"
        "G += (mid - G) * springK * grow\n"
        "tang = b - a\n"
        "nrm = normalize(vec3(0.0 - tang.y, tang.x, 0.0))\n"
        "G += nrm * (repel * sin(t * speed + i * detail)) * grow\n"
        "isEnd = max(if(i < 0.5, 1.0, 0.0), if(i > count - 1.5, 1.0, 0.0))\n"
        "G = mix(G, P, isEnd)\n"
        "P = G\n"
        "Cd = vec3(0.35 + 0.5 * sin(i * detail), 0.4, 0.95)\n"
        "publish = sin(t * speed)\n" },
      { "Vortex Swirl & Strain Glow",
        "param float twist = 1.5 [0.0, 5.0]\n"
        "param float freq = 4.0 [0.5, 12.0]\n"
        "param float amp = 0.3 [0.0, 1.5]\n"
        "param float speed = 1.5 [0.0, 6.0]\n"
        "px = P.x\n"
        "pz = P.z\n"
        "r = length(vec2(px, pz))\n\n"
        "angle = (twist / (r + 0.3)) + t * speed\n"
        "cosA = cos(angle)\n"
        "sinA = sin(angle)\n\n"
        "P.x = px * cosA - pz * sinA\n"
        "P.z = px * sinA + pz * cosA\n\n"
        "wave = sin(r * freq - t * speed * 2.0)\n"
        "P.y += wave * amp / (1.0 + r * 0.5)\n\n"
        "strain = abs(wave) / (1.0 + r)\n"
        "Cd = vec3(0.1 + 0.8 * strain, 0.4 + 0.4 * sin(angle), 0.9 - 0.5 * strain)\n\n"
        "output frame float peakStrain = reduce.max(strain)\n"
        "publish = peakStrain\n" },
      { "Organic Breathing Harmonic",
        "param float amp = 0.25 [0.0, 1.0]\n"
        "param float freq = 3.5 [0.5, 10.0]\n"
        "param float speed = 2.0 [0.0, 8.0]\n"
        "disp = sin(P.x * freq + t * speed) * cos(P.y * freq - t * speed * 0.7) * sin(P.z * freq + t * speed * 1.3) * amp\n"
        "P += N * disp\n\n"
        "crest = clamp((disp / (amp + 0.0001)) * 0.5 + 0.5, 0.0, 1.0)\n"
        "Cd = vec3(0.05 + 0.3 * crest, 0.2 + 0.8 * crest, 0.5 + 0.5 * crest)\n\n"
        "elev = length(P)\n"
        "output frame float avgRadius = reduce.mean(elev)\n"
        "publish = avgRadius\n" },
      { "Neighbor-Deviation Shatter",
        "param float jitter = 0.15 [0.0, 0.6]\n"
        "param float facetSize = 0.08 [0.02, 0.3]\n"
        "param float speed = 0.6 [0.0, 3.0]\n"
        "a = P.at(i - 1)\n"
        "b = P.at(i + 1)\n"
        "localAvg = (a + b + P) * 0.3333333\n"
        "dev = P - localAvg\n"
        "devLen = length(dev)\n"
        "h = rand(0.0, 1.0, 0.0, i)\n"
        "offsetDir = normalize(dev + vec3(0.0001, 0.0, 0.0))\n"
        "P += offsetDir * (h - 0.5) * jitter * facetSize * 10.0\n"
        "Cd = vec3(0.5 + 0.5 * sin(i * 1.7 + t * speed), 0.4 + 0.4 * h, 0.6)\n"
        "publish = reduce.mean(devLen)\n" },
      { "Traveling Wavefront Cloth",
        "param float speed1 = 1.2 [0.1, 4.0]\n"
        "param float speed2 = 0.7 [0.1, 4.0]\n"
        "param float freq1 = 3.0 [0.5, 10.0]\n"
        "param float freq2 = 4.5 [0.5, 10.0]\n"
        "param float amp = 0.25 [0.0, 1.0]\n"
        "dir1 = vec2(0.866, 0.5)\n"
        "dir2 = vec2(-0.5, 0.866)\n"
        "pxz = P.xz\n"
        "d1 = dot(pxz, dir1)\n"
        "d2 = dot(pxz, dir2)\n"
        "w1 = sin(d1 * freq1 - t * speed1)\n"
        "w2 = sin(d2 * freq2 + t * speed2)\n"
        "P.y += (w1 + w2) * 0.5 * amp\n"
        "Cd = vec3(0.5 + 0.5 * w1, 0.5 + 0.5 * w2, 0.8)\n"
        "publish = sin(t * speed1)\n" }
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

Field::DeviceFile FieldElementNode::ToDeviceFile() const
{
   Field::DeviceFile device;
   device.domain = "element";
   device.code = code;
   for (const auto& p : mParamTable.Params())
   {
      if (p.isDeclared)
         device.params[p.name] = p.value;
   }
   device.nodeSettings["maxElements"] = (double)maxElements;
   // Only meaningful when nothing is wired into `input` - see the
   // `if (!n->input)` guard around this same field in main.cpp.
   if (!input)
      device.nodeSettings["generateCount"] = (double)generateCount;
   return device;
}

void FieldElementNode::LoadDeviceFile(const Field::DeviceFile& device)
{
   code = device.code;
   auto itMax = device.nodeSettings.find("maxElements");
   if (itMax != device.nodeSettings.end())
      maxElements = (int)itMax->second;
   auto itGen = device.nodeSettings.find("generateCount");
   if (itGen != device.nodeSettings.end())
      generateCount = (int)itGen->second;
   Apply();
   // Param values are matched by name against whatever the freshly-compiled
   // program actually declared - a name the target's code doesn't declare
   // is silently skipped (Find returns nullptr), never phantom-added.
   for (const auto& kv : device.params)
   {
      Field::ParamEntry* p = mParamTable.Find(kv.first);
      if (p != nullptr)
         p->value = kv.second;
   }
}

FieldElementNode::FieldElementNode()
{
   mPublishOutput.owner = this;
   // Seed from the first factory preset, not a bare unparented stub: the
   // stub declares no `param float` lines, so a freshly spawned node
   // compiled zero params into GetParamTable() while presetIndex (defaulted
   // to 0) still pointed the dropdown label at Presets()[0]'s name - see
   // FieldSampleNode's constructor for the identical bug and full writeup.
   const auto& presets = Presets();
   if (!presets.empty())
      code = presets[0].code;
   else
      code = "P.y += sin(P.x * 2.0 + t) * 0.2\n";
   Apply();
}

bool FieldElementNode::Apply()
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

   // Dynamic pins, Phase 2b (build step 13, §5.1): reconcile the declared
   // output/input pin tables against this compile's irProgram.declaredOutputs
   // / declaredInputs (populated by LowerElementProgramToIR above - NOT on
   // `prog`, which doesn't carry them). Must run before any other live
   // state is mutated so a refusal here leaves the whole Apply() a no-op,
   // same as a compile error.
   {
      std::vector<Field::DeclaredPin> declOut, declIn;
      for (const auto& d : irProgram.declaredOutputs)
         declOut.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), true });
      for (const auto& d : irProgram.declaredInputs)
         declIn.push_back({ d.name, d.typeName, Field::DomainToString(d.domain), false });

      std::string pinNotice, pinRefusal;
      bool outOk = Field::ReconcileFieldPins(mOutputPins, declOut, mNodeIndex, NativeOutputCount(), pinNotice, pinRefusal);
      bool inOk = outOk && Field::ReconcileFieldPins(mInputPins, declIn, mNodeIndex, /*nativeCount=*/1, pinNotice, pinRefusal);
      if (!outOk || !inOk)
      {
         mLastError = pinRefusal;
         this->pinRefusal = pinRefusal;
         return false;
      }
      if (!pinNotice.empty())
         mNotice = pinNotice;
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
   mStateAge = 0.0f;
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
      mStateAge = 0.0f;
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

   std::map<std::string, float> paramValues = mParamTable.ValueMap();
   size_t paramHash = 0;
   for (const auto& kv : paramValues)
   {
      paramHash ^= std::hash<std::string>{}(kv.first) + 0x9e3779b9 + (paramHash << 6) + (paramHash >> 2);
      paramHash ^= std::hash<float>{}(kv.second) + 0x9e3779b9 + (paramHash << 6) + (paramHash >> 2);
   }

   bool needRebuild = (upRev != mLastUpstreamRevision) ||
                      (mProgram && mProgram->isTimeDependent && (float)t != mLastEvalT) ||
                      (mOutMesh.vertices.empty()) ||
                      (!mState.Cells().empty()) ||
                      (paramHash != mLastParamHash);

   if (!needRebuild)
      return;

   mLastParamHash = paramHash;

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
      // Resizing the bank zeroes it, so the simulation is starting over: `age`
      // has to say so, or a one-shot seed would never fire again.
      mStateAge = 0.0f;
   }

   // 1. Gather (AoS -> SoA)
   mStore.FromMesh(inMesh, boundCount);

   // 2. Execute Element VM
   Field::ExecutionEnv env;
   env.t = t;
   env.dt = (mLastEvalT > -900000.0f) ? (t - mLastEvalT) : (1.0 / 60.0);
   env.frame = (double)frameId;
   env.age = (double)mStateAge;
   env.params = &paramValues;
   env.state = &mState;
   std::string vmErr;
   mVM.Execute(*mProgram, mStore, env, vmErr);

   // 3. Scatter (SoA -> AoS)
   mStore.ToMesh(mOutMesh, mProgram->WriteMask(), inMesh, boundCount);

   mStateAge += 1.0f;

   mMeshRevision = NextMeshRevision();
   mLastUpstreamRevision = upRev;
   mLastEvalT = (float)t;
}
