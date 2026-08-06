#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

class INode;

// A node that emits a control value instead of an image. Modulators are patched
// into individual parameters rather than into image inputs. The binding always
// normalises: a modulator speaks in 0..1 and each destination maps that onto its
// own range, so one macro can drive parameters with wildly different scales.
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

   struct Source
   {
      int nodeIndex = -1;
      int outputIndex = 0;
   };

   void Bind(int nodeIndex, int paramIndex, int modulatorNodeIndex, int outputIndex = 0);
   void Unbind(int nodeIndex, int paramIndex);
   void UnbindAllFor(int nodeIndex); // node deleted: drop it as target and as source

   // nodeIndex is -1 when the parameter is not modulated.
   Source ModulatorFor(int nodeIndex, int paramIndex) const;
   bool IsModulated(int nodeIndex, int paramIndex) const { return ModulatorFor(nodeIndex, paramIndex).nodeIndex >= 0; }

   const std::map<Key, Source>& Links() const { return mLinks; }

   // Parameters registered during the current frame's node drawing.
   void ClearFrameParams() { mFrameParams.clear(); }
   void RegisterParam(const ParamRef& ref) { mFrameParams.push_back(ref); }
   const std::vector<ParamRef>& FrameParams() const { return mFrameParams; }

private:
   std::map<Key, Source> mLinks;
   std::vector<ParamRef> mFrameParams;
};
