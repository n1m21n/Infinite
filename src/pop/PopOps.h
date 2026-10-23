#pragma once

// The PointZ operator table. Every PointZ node is one PopDef: a name, its pins, a
// declarative parameter list and a cook function. Kernel operators (tier A,
// one GPU pass per point) build a KernelSpec in their cook; topology and
// simulation operators (tier B) work on CPU copies. PopNode (nodes/PopNode.h)
// is the single INode class that runs any of them, so adding an operator is
// one table entry and never touches the app's dispatch code.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "PopData.h"
#include "PopGpu.h"

namespace pop
{
   struct ParamSpec
   {
      enum Type { kFloat = 0, kInt, kBool, kMenu, kColor, kText, kVec3 };
      std::string key;
      std::string label;
      Type type = kFloat;
      float def[3] = { 0, 0, 0 };
      float lo = 0.0f, hi = 1.0f;
      std::vector<std::string> menu;
      std::string defText;
      // UI only: show this control when another param has one of these values,
      // written "key=v1|v2" (menu indices or 0/1 for bools). Empty = always.
      std::string showIf;
      int Floats() const { return (type == kVec3 || type == kColor) ? 3 : (type == kText ? 0 : 1); }
   };

   // Builders, so a table entry reads like a parameter page.
   ParamSpec F(const char* key, const char* label, float def, float lo, float hi, const char* showIf = "");
   ParamSpec I(const char* key, const char* label, int def, int lo, int hi, const char* showIf = "");
   ParamSpec B(const char* key, const char* label, bool def, const char* showIf = "");
   ParamSpec M(const char* key, const char* label, std::vector<std::string> items, int def, const char* showIf = "");
   ParamSpec C(const char* key, const char* label, float r, float g, float b, const char* showIf = "");
   ParamSpec T(const char* key, const char* label, const char* def, const char* showIf = "");
   ParamSpec V(const char* key, const char* label, float x, float y, float z, float lo, float hi, const char* showIf = "");

   struct ParamValues
   {
      std::vector<float> f;        // floats of every non-text param, in order
      std::vector<std::string> s;  // one entry per param (text params only use it)
   };

   struct PopDef;

   // Per-node persistent state for operators with memory (Particle,
   // Feedback, Trail, Cache ...). Owned by the node, reset when the node is
   // reset or its input topology changes in a way the operator rejects.
   struct OpState
   {
      virtual ~OpState() {}
   };

   struct CookCtx
   {
      const PopDef* def = nullptr;
      const ParamValues* values = nullptr;
      DataPtr in[4];
      int inCount = 0;
      unsigned int image = 0;
      int imageW = 0, imageH = 0;
      unsigned int images[4] = { 0, 0, 0, 0 }; // every image pin (image == images[0])
      KernelEnv env;
      std::unique_ptr<OpState>* state = nullptr;
      bool reset = false;          // the node asked for a restart this cook
      // Another PointZ by its 'tag' (Feedback / Particle targets, Cache Select):
      // its output as of the last cook, and its operator state.
      std::function<const PopData*(const std::string&)> findTagged;
      std::function<OpState*(const std::string&)> findTaggedState;

      std::shared_ptr<PopData> out;
      // Instance PointZ: points to draw `out` at (see IGeometrySource::GetPopInstances).
      std::shared_ptr<PopData> instances;
      std::string warning;         // shown as the node's red-border message
      std::string error;           // cook failed; output stays empty

      float F(const std::string& key) const;
      int I(const std::string& key) const { return (int)F(key); }
      bool Bl(const std::string& key) const { return F(key) != 0.0f; }
      void V3(const std::string& key, float out3[3]) const;
      const std::string& S(const std::string& key) const;
      const PopData* Input(int i) const { return (i >= 0 && i < inCount && in[i]) ? in[i].get() : nullptr; }

      // Every number param as a uniform p_<key> (float, int for int/bool/menu,
      // vec3 for vectors and colours).
      std::vector<KernelUniform> ParamUniforms() const;
      // Runs a kernel over `src` with the param uniforms (+ extra) and the env;
      // writes the results into `out` (which must already hold src's data).
      bool Kernel(const KernelSpec& spec, const PopData& src, const std::vector<KernelUniform>& extra = {});

      template <class S> S& State()
      {
         if (!*state || dynamic_cast<S*>(state->get()) == nullptr)
            state->reset(new S());
         return *static_cast<S*>(state->get());
      }
   };

   struct PopDef
   {
      std::string name;             // palette name, e.g. "Noise PointZ"
      std::string tdName;           // the TouchDesigner operator it follows
      std::string help;
      int inputs = 1;               // geometry pins
      std::vector<std::string> inputLabels;
      bool imageInput = false;      // image pins after the geometry pins
      std::string imageLabel = "image";
      int imageCount = 1;           // how many (up to 4) when imageInput
      std::vector<std::string> imageLabels; // per pin; imageLabel when missing
      bool everyFrame = false;      // time-driven or stateful: cooks every frame
      // Cooks every frame only while this param is non-zero (a "speed" knob).
      std::string animKey;
      char tier = 'A';
      std::vector<ParamSpec> params;
      // Turbo 0.70: params whose absence from a saved patch means "saved before
      // this existed" - set to these values before the saved ones load
      // (INode::BeforeLoadParams), so old patches keep their behaviour while a
      // node made from the picker gets the param's default.
      std::vector<std::pair<std::string, float>> savedBefore;
      std::function<bool(CookCtx&)> cook;

      int ParamIndex(const std::string& key) const;
      int FloatOffset(int paramIndex) const;
      void Defaults(ParamValues& v) const;
   };

   const std::vector<PopDef>& Registry();
   const PopDef* FindDef(const std::string& name);

   // Turbo 0.71 (P1): Noise PointZ's curl over 4D gradient noise reuses each
   // lattice cell's gradients across its samples; true puts the plain kernel
   // back (the self-test's bit-for-bit reference).
   void SetNoiseReferenceKernelForTest(bool on);
   // Turbo 0.71 (P1): Particle PointZ (CPU engine) copies back only the rows
   // its next births can pick from a GPU source; true copies the whole source
   // again, as before (the self-test's reference).
   void SetParticleFullSourceCopyForTest(bool on);

   // Registration of the two operator tiers (PopOpsA.cpp, PopOpsB.cpp).
   void RegisterTierA(std::vector<PopDef>& defs);
   void RegisterGenerators(std::vector<PopDef>& defs); // part of tier A (PopOpsGen.cpp)
   // Operators that need the platform layer (fonts): PopOpsPlatform.cpp.
   void RegisterPlatformOps(std::vector<PopDef>& defs);
   void RegisterTierB(std::vector<PopDef>& defs);

   // --- helpers shared by operator files ----------------------------------
   // A per-point kernel operator: reads/writes fixed attributes, body in GLSL.
   // When `masked` the op gains a "mask" text param naming a float attribute
   // that blends the result per point (0 keeps the input, 1 applies fully).
   PopDef KernelOp(const char* name, const char* td, const char* help, std::vector<ParamSpec> params,
                   std::vector<KernelIO> reads, std::vector<KernelIO> writes, const char* body,
                   bool masked = true, bool everyFrame = false);
   // Start an output as a shallow copy of input 0 (or empty when none).
   bool PassInput(CookCtx& c);
   // Topology helpers.
   std::shared_ptr<PopTopology> NewTopo();
   std::shared_ptr<PopData> FromPoints(std::vector<float>&& P, bool asPointPrims);
}
