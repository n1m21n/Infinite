#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

class INode;

// A node that emits a control value instead of an image. Modulators are patched
// into individual parameters rather than into image inputs.
class IModulator
{
public:
   virtual ~IModulator() {}

   // Current value, normalised 0..1. The binding maps it onto the parameter's range.
   virtual float Value01() = 0;
};

// One modulatable parameter, re-registered every frame while its node draws.
// The raw float* is only ever used within the frame that registered it.
struct ParamRef
{
   int nodeIndex = 0;
   int paramIndex = 0;
   float* value = nullptr;
   float minValue = 0.0f;
   float maxValue = 1.0f;
   std::string name;
};

// Which modulator drives which parameter. Keyed by (nodeIndex, paramIndex) so the
// binding survives nodes being redrawn, and stores the modulator's node index so a
// deleted modulator can be unbound.
class Modulation
{
public:
   using Key = std::pair<int, int>;

   static Modulation& Instance();

   void Bind(int nodeIndex, int paramIndex, int modulatorNodeIndex);
   void Unbind(int nodeIndex, int paramIndex);
   void UnbindAllFor(int nodeIndex); // node deleted: drop it as target and as source

   // -1 when the parameter is not modulated.
   int ModulatorFor(int nodeIndex, int paramIndex) const;
   bool IsModulated(int nodeIndex, int paramIndex) const { return ModulatorFor(nodeIndex, paramIndex) >= 0; }

   const std::map<Key, int>& Links() const { return mLinks; }

   // Parameters registered during the current frame's node drawing.
   void ClearFrameParams() { mFrameParams.clear(); }
   void RegisterParam(const ParamRef& ref) { mFrameParams.push_back(ref); }
   const std::vector<ParamRef>& FrameParams() const { return mFrameParams; }

private:
   std::map<Key, int> mLinks;
   std::vector<ParamRef> mFrameParams;
};
