#include "Sketch3DNode.h"

#include "core/sketch/Sketch3DPresets.h"
#include "Transport.h"

#include <cmath>

const std::vector<Sketch3DNode::Preset>& Sketch3DNode::Presets()
{
   static const std::vector<Preset> p = [] {
      std::vector<Preset> v;
      int n = 0;
      const Sketch3DPresets::Entry* e = Sketch3DPresets::All(n);
      for (int i = 0; i < n; ++i) v.push_back({e[i].name, e[i].code});
      return v;
   }();
   return p;
}

const std::vector<std::string>& Sketch3DNode::PresetNames()
{
   static const std::vector<std::string> n = [] {
      std::vector<std::string> v;
      for (const auto& p : Presets()) v.push_back(p.name);
      return v;
   }();
   return n;
}

Sketch3DNode::Sketch3DNode() { code = Presets()[0].code; }

void Sketch3DNode::LoadPreset(int index)
{
   const auto& p = Presets();
   if (index >= 0 && index < (int)p.size())
   {
      code = p[index].code;
      Apply();
   }
}

bool Sketch3DNode::Apply()
{
   mCompiledOnce = true;
   mCompiledCode = code;
   SketchError err;
   if (!mEngine.Compile(code, err))
   {
      mLastError = err.line > 0 ? "line " + std::to_string(err.line) + ": " + err.message : err.message;
      return false;
   }
   mLastError.clear();
   std::vector<Field::DeclaredParam> decl;
   for (const auto& p : mEngine.Params())
   {
      Field::DeclaredParam d;
      d.name = p.name; d.typeName = "float";
      d.defaultValue = p.def; d.minValue = p.lo; d.maxValue = p.hi;
      decl.push_back(d);
   }
   mParamTable.Reconcile(decl, mNodeIndex, mNotice);
   mHasRun = false;   // new program: rebuild even if time and params match
   return true;
}

void Sketch3DNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId) return;
   mLastCookFrame = frameId;

   if (!mCompiledOnce || code != mCompiledCode) Apply();
   if (!mEngine.HasProgram()) return;

   const double t = animate ? Transport::Instance().Seconds() : 0.0;
   std::map<std::string, float> pm = mParamTable.ValueMap();
   if (mHasRun && t == mRunT && pm == mRunParams) return;

   Sketch3DEngine::Frame f;
   f.t = t;
   f.beat = animate ? Transport::Instance().Beats() : 0.0;
   f.frame = (int)std::floor(t * 60.0);
   f.seed = (uint32_t)(mNodeIndex + 1);
   f.params = &pm;

   SketchError err;
   Mesh built;
   const bool ok = mEngine.Run(f, built, err);
   mHasRun = true; mRunT = t; mRunParams = pm;
   if (!ok)
   {
      // Keep the last good mesh on screen.
      mLastError = err.line > 0 ? "line " + std::to_string(err.line) + ": " + err.message : err.message;
      return;
   }
   mLastError.clear();
   mMesh = std::move(built);
   mMeshRevision = NextMeshRevision();
}
