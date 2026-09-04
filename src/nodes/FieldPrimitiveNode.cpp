#include "FieldPrimitiveNode.h"
#include "field/FieldLex.h"
#include "field/FieldParse.h"
#include "Transport.h"

#include <algorithm>
#include <cmath>

const std::vector<FieldPrimitiveNode::Preset>& FieldPrimitiveNode::Presets()
{
   static const std::vector<Preset> kPresets = {
      { "Solid Terrain Plane",
        PrimitiveTopology::Plane,
        2500,
        "param float size = 2.5 [0.5, 10.0]\n"
        "param float height = 0.40 [0.0, 2.0]\n"
        "param float freq = 2.5 [0.5, 8.0]\n"
        "param float speed = 1.0 [0.0, 5.0]\n"
        "x = (uv.x - 0.5) * size\n"
        "z = (uv.y - 0.5) * size\n"
        "dist = sqrt(x * x + z * z)\n"
        "wave = sin(dist * freq - (t + 1.0) * speed)\n"
        "y = wave * height * exp(-dist * 0.4)\n"
        "P = vec3(x, y, z)\n"
        "slope = cos(dist * freq - (t + 1.0) * speed) * freq * height * exp(-dist * 0.4)\n"
        "nx = if(dist > 0.001, -(x / dist) * slope, 0.0)\n"
        "nz = if(dist > 0.001, -(z / dist) * slope, 0.0)\n"
        "N = normalize(vec3(nx, 1.0, nz))\n"
        "Cd = vec3(0.15 + 0.35 * (y + 0.5), 0.55 + 0.4 * sin(dist * 3.0), 0.85)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid UV Sphere",
        PrimitiveTopology::Sphere,
        2400,
        "param float radius = 1.2 [0.2, 5.0]\n"
        "param float ripple = 0.15 [0.0, 0.6]\n"
        "param float freq = 6.0 [1.0, 16.0]\n"
        "param float speed = 1.5 [0.0, 5.0]\n"
        "phi = uv.x * 6.2831853\n"
        "theta = (uv.y - 0.5) * 3.14159265\n"
        "disp = 1.0 + ripple * sin(phi * freq + (t + 1.0) * speed) * cos(theta * freq)\n"
        "r = radius * disp\n"
        "px = cos(theta) * cos(phi) * r\n"
        "py = sin(theta) * r\n"
        "pz = cos(theta) * sin(phi) * r\n"
        "P = vec3(px, py, pz)\n"
        "N = normalize(P)\n"
        "Cd = vec3(0.5 + 0.5 * N.x, 0.5 + 0.5 * N.y, 0.5 + 0.5 * N.z)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid Torus",
        PrimitiveTopology::Torus,
        2500,
        "param float rMajor = 1.3 [0.3, 4.0]\n"
        "param float rMinor = 0.45 [0.05, 1.5]\n"
        "param float twist = 3.0 [0.0, 10.0]\n"
        "param float speed = 1.2 [0.0, 5.0]\n"
        "phi = uv.x * 6.2831853\n"
        "theta = uv.y * 6.2831853 + phi * twist + (t + 1.0) * speed\n"
        "r = rMajor + rMinor * cos(theta)\n"
        "P = vec3(r * cos(phi), rMinor * sin(theta), r * sin(phi))\n"
        "N = vec3(cos(theta) * cos(phi), sin(theta), cos(theta) * sin(phi))\n"
        "Cd = vec3(0.5 + 0.5 * cos(phi), 0.5 + 0.5 * sin(theta), 0.9)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid Cylinder",
        PrimitiveTopology::Cylinder,
        2400,
        "param float radius = 0.8 [0.1, 3.0]\n"
        "param float height = 2.4 [0.2, 8.0]\n"
        "param float taper = 0.3 [0.0, 0.9]\n"
        "param float twist = 2.0 [0.0, 8.0]\n"
        "param float speed = 1.0 [0.0, 5.0]\n"
        "phi = uv.x * 6.2831853 + uv.y * twist + (t + 1.0) * speed\n"
        "y = (uv.y - 0.5) * height\n"
        "r = radius * (1.0 - uv.y * taper)\n"
        "P = vec3(cos(phi) * r, y, sin(phi) * r)\n"
        "N = vec3(cos(phi), taper * 0.2, sin(phi))\n"
        "Cd = vec3(0.2, 0.5 + 0.5 * uv.y, 0.9)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Solid Mobius Ribbon",
        PrimitiveTopology::Plane,
        2500,
        "param float radius = 1.3 [0.3, 4.0]\n"
        "param float width = 0.5 [0.05, 2.0]\n"
        "param float twists = 1.0 [1.0, 5.0]\n"
        "param float speed = 1.0 [0.0, 4.0]\n"
        "u = uv.x * 6.2831853 + (t + 1.0) * speed\n"
        "v = (uv.y - 0.5) * width\n"
        "halfA = u * twists * 0.5\n"
        "r = radius + v * cos(halfA)\n"
        "P = vec3(r * cos(u), v * sin(halfA), r * sin(u))\n"
        "N = vec3(-sin(u), cos(halfA), cos(u))\n"
        "Cd = vec3(0.5 + 0.5 * cos(u), 0.5 + 0.5 * sin(halfA), 0.8)\n"
        "publish = sin((t + 1.0) * speed)\n" },
      { "Fibonacci Sphere Points",
        PrimitiveTopology::Points,
        2000,
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
      { "Spiral Curve",
        PrimitiveTopology::Points,
        1000,
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
      { "Helix Curve",
        PrimitiveTopology::Points,
        1000,
        "param float radius = 0.8 [0.1, 3.0]\n"
        "param float pitch = 2.0 [0.2, 8.0]\n"
        "param float turns = 3.0 [1.0, 15.0]\n"
        "param float speed = 2.0 [0.0, 10.0]\n"
        "u = i / count\n"
        "a = u * turns * 6.2831853 + t * speed\n"
        "P = vec3(cos(a) * radius, (u - 0.5) * pitch, sin(a) * radius)\n"
        "Cd = vec3(0.3, 0.5 + 0.5 * sin(a), 0.9)\n"
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
      topology = (int)presets[index].topology;
      if (presets[index].defaultCount > 0)
         count = presets[index].defaultCount;
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
   device.nodeSettings["topology"] = (double)topology;
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
   auto itTop = device.nodeSettings.find("topology");
   if (itTop != device.nodeSettings.end())
      topology = (int)itTop->second;
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
   const auto& presets = Presets();
   if (!presets.empty())
   {
      code = presets[0].code;
      topology = (int)presets[0].topology;
      count = presets[0].defaultCount;
   }
   else
   {
      code = "u = i / count\nangle = u * 6.2831853\nP = vec3(cos(angle), sin(angle), 0.0)\n";
      topology = 0;
      count = 256;
   }
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
                      (topology != mLastBuiltTopology) ||
                      (mProgram && mProgram->isTimeDependent && (float)t != mLastEvalT) ||
                      (mOutMesh.vertices.empty()) ||
                      (!mState.Cells().empty()) ||
                      (paramHash != mLastParamHash);

   if (!needRebuild)
      return;

   mLastParamHash = paramHash;

   // Synthesize base scaffold mesh according to topology
   Mesh baseMesh;
   auto top = static_cast<PrimitiveTopology>(topology);
   if (top == PrimitiveTopology::Plane)
   {
      int cols = std::max(2, (int)std::round(std::sqrt((double)boundCount)));
      int rows = std::max(2, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float u = (cols > 1) ? ((float)c / (float)(cols - 1)) : 0.0f;
         float v = (rows > 1) ? ((float)r / (float)(rows - 1)) : 0.0f;
         baseMesh.vertices[idx].px = (u - 0.5f) * 2.0f;
         baseMesh.vertices[idx].py = 0.0f;
         baseMesh.vertices[idx].pz = (v - 0.5f) * 2.0f;
         baseMesh.vertices[idx].nx = 0.0f;
         baseMesh.vertices[idx].ny = 1.0f;
         baseMesh.vertices[idx].nz = 0.0f;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows - 1; ++r)
      {
         for (int c = 0; c < cols - 1; ++c)
         {
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + (c + 1));
            unsigned int i01 = (unsigned int)((r + 1) * cols + c);
            unsigned int i11 = (unsigned int)((r + 1) * cols + (c + 1));
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i10);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Sphere)
   {
      int cols = std::max(4, (int)std::round(std::sqrt((double)boundCount * 1.5)));
      int rows = std::max(3, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float v = (rows > 1) ? ((float)r / (float)(rows - 1)) : 0.0f;
         float theta = (v - 0.5f) * 3.14159265f;
         float cosTheta = std::cos(theta);
         float sinTheta = std::sin(theta);
         float u = (float)c / (float)cols;
         float phi = u * 6.2831853f;
         float cosPhi = std::cos(phi);
         float sinPhi = std::sin(phi);
         baseMesh.vertices[idx].px = cosTheta * cosPhi;
         baseMesh.vertices[idx].py = sinTheta;
         baseMesh.vertices[idx].pz = cosTheta * sinPhi;
         baseMesh.vertices[idx].nx = baseMesh.vertices[idx].px;
         baseMesh.vertices[idx].ny = baseMesh.vertices[idx].py;
         baseMesh.vertices[idx].nz = baseMesh.vertices[idx].pz;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows - 1; ++r)
      {
         for (int c = 0; c < cols; ++c)
         {
            int nextC = (c + 1) % cols;
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + nextC);
            unsigned int i01 = (unsigned int)((r + 1) * cols + c);
            unsigned int i11 = (unsigned int)((r + 1) * cols + nextC);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i01);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Cylinder)
   {
      int cols = std::max(4, (int)std::round(std::sqrt((double)boundCount * 1.5)));
      int rows = std::max(2, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float v = (rows > 1) ? ((float)r / (float)(rows - 1)) : 0.0f;
         float y = (v - 0.5f) * 2.0f;
         float u = (float)c / (float)cols;
         float phi = u * 6.2831853f;
         float cosPhi = std::cos(phi);
         float sinPhi = std::sin(phi);
         baseMesh.vertices[idx].px = cosPhi;
         baseMesh.vertices[idx].py = y;
         baseMesh.vertices[idx].pz = sinPhi;
         baseMesh.vertices[idx].nx = cosPhi;
         baseMesh.vertices[idx].ny = 0.0f;
         baseMesh.vertices[idx].nz = sinPhi;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows - 1; ++r)
      {
         for (int c = 0; c < cols; ++c)
         {
            int nextC = (c + 1) % cols;
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + nextC);
            unsigned int i01 = (unsigned int)((r + 1) * cols + c);
            unsigned int i11 = (unsigned int)((r + 1) * cols + nextC);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i11);
               baseMesh.indices.push_back(i01);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Torus)
   {
      int cols = std::max(4, (int)std::round(std::sqrt((double)boundCount)));
      int rows = std::max(4, (int)(boundCount / (size_t)cols));
      baseMesh.vertices.resize(boundCount);
      float R = 1.0f;
      float rMinor = 0.35f;
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int r = std::min((int)(idx / (size_t)cols), rows - 1);
         int c = std::min((int)(idx % (size_t)cols), cols - 1);
         float v = (float)r / (float)rows;
         float theta = v * 6.2831853f;
         float cosTheta = std::cos(theta);
         float sinTheta = std::sin(theta);
         float u = (float)c / (float)cols;
         float phi = u * 6.2831853f;
         float cosPhi = std::cos(phi);
         float sinPhi = std::sin(phi);
         float dist = R + rMinor * cosTheta;
         baseMesh.vertices[idx].px = dist * cosPhi;
         baseMesh.vertices[idx].py = rMinor * sinTheta;
         baseMesh.vertices[idx].pz = dist * sinPhi;
         baseMesh.vertices[idx].nx = cosTheta * cosPhi;
         baseMesh.vertices[idx].ny = sinTheta;
         baseMesh.vertices[idx].nz = cosTheta * sinPhi;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int r = 0; r < rows; ++r)
      {
         int nextR = (r + 1) % rows;
         for (int c = 0; c < cols; ++c)
         {
            int nextC = (c + 1) % cols;
            unsigned int i00 = (unsigned int)(r * cols + c);
            unsigned int i10 = (unsigned int)(r * cols + nextC);
            unsigned int i01 = (unsigned int)(nextR * cols + c);
            unsigned int i11 = (unsigned int)(nextR * cols + nextC);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i11);
            }
         }
      }
   }
   else if (top == PrimitiveTopology::Disc)
   {
      int rings = std::max(2, (int)std::round(std::sqrt((double)boundCount * 0.5)));
      int sectors = std::max(3, (int)(boundCount / (size_t)rings));
      baseMesh.vertices.resize(boundCount);
      for (size_t idx = 0; idx < boundCount; ++idx)
      {
         int rg = std::min((int)(idx / (size_t)sectors), rings - 1);
         int s = std::min((int)(idx % (size_t)sectors), sectors - 1);
         float v = (rings > 1) ? ((float)rg / (float)(rings - 1)) : 0.0f;
         float rad = v;
         float u = (float)s / (float)sectors;
         float phi = u * 6.2831853f;
         baseMesh.vertices[idx].px = std::cos(phi) * rad;
         baseMesh.vertices[idx].py = 0.0f;
         baseMesh.vertices[idx].pz = std::sin(phi) * rad;
         baseMesh.vertices[idx].nx = 0.0f;
         baseMesh.vertices[idx].ny = 1.0f;
         baseMesh.vertices[idx].nz = 0.0f;
         baseMesh.vertices[idx].u = u;
         baseMesh.vertices[idx].v = v;
      }
      for (int rg = 0; rg < rings - 1; ++rg)
      {
         for (int s = 0; s < sectors; ++s)
         {
            int nextS = (s + 1) % sectors;
            unsigned int i00 = (unsigned int)(rg * sectors + s);
            unsigned int i10 = (unsigned int)(rg * sectors + nextS);
            unsigned int i01 = (unsigned int)((rg + 1) * sectors + s);
            unsigned int i11 = (unsigned int)((rg + 1) * sectors + nextS);
            if (i00 < boundCount && i10 < boundCount && i01 < boundCount && i11 < boundCount)
            {
               baseMesh.indices.push_back(i00);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i10);
               baseMesh.indices.push_back(i01);
               baseMesh.indices.push_back(i11);
            }
         }
      }
   }
   else // Points (topology == 0)
   {
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
   }

   if (!mProgram)
   {
      mOutMesh = baseMesh;
      mMeshRevision = NextMeshRevision();
      mLastBuiltCount = (unsigned long long)boundCount;
      mLastBuiltTopology = topology;
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
   mLastBuiltTopology = topology;
   mLastEvalT = (float)t;
}
