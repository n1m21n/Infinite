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
      // A binding's polarity is a property of the *connection*, not the
      // modulator - see docs/plans/modulators/README.md. kAbsolute is the
      // default and must stay the default: it is today's override behaviour,
      // and every existing patch and every freshly-dragged cable has to load
      // and behave exactly as it always has.
      enum Polarity { kAbsolute = 0, kBipolar };

      int nodeIndex = -1;
      int outputIndex = 0;
      int polarity = kAbsolute;
      // Only meaningful when polarity == kBipolar. -1..1, matching
      // ModDepthNode's convention (negative inverts).
      float depth = 1.0f;
      // The destination parameter's own value at the instant the binding was
      // created (or, for a patch load, whatever was saved) - the centre a
      // kBipolar binding swings around. Captured once and never touched
      // again by the apply loop, since *value is being overwritten by the
      // modulator every frame from then on and can't be read back safely.
      float centre = 0.0f;
   };

   // Creates a fresh binding, always kAbsolute, capturing the destination
   // parameter's current value as `centre` (read through this frame's
   // FrameParams - see the .cpp for why that's safe at every real call site).
   void Bind(int nodeIndex, int paramIndex, int modulatorNodeIndex, int outputIndex = 0);
   // Patch load only: installs a Source exactly as read from disk, including
   // its persisted centre - unlike Bind(), it must NOT recompute centre from
   // the current frame, since the node whose value should become centre may
   // not have drawn a single frame yet.
   void RestoreLink(int nodeIndex, int paramIndex, const Source& source);
   // Changes an existing binding's polarity/depth in place, leaving
   // nodeIndex/outputIndex/centre untouched. No-op if nothing is bound there.
   void SetPolarity(int nodeIndex, int paramIndex, int polarity, float depth);
   void Unbind(int nodeIndex, int paramIndex);
   void UnbindAllFor(int nodeIndex); // node deleted: drop it as target and as source

   // nodeIndex is -1 when the parameter is not modulated.
   Source ModulatorFor(int nodeIndex, int paramIndex) const;
   bool IsModulated(int nodeIndex, int paramIndex) const { return ModulatorFor(nodeIndex, paramIndex).nodeIndex >= 0; }

   const std::map<Key, Source>& Links() const { return mLinks; }

   // Inline algebraic expressions typed directly into a parameter field
   // (e.g. "sin(t) * 0.5 + 0.5"), keyed the same way as mLinks. A parameter
   // can be driven by a wired modulator or by a typed expression, but not
   // both at once: if a link exists for a param that also carries an
   // expression, the wired modulator wins (see the apply loop in main.cpp),
   // which matches the UI already locking the field read-only the moment
   // something is patched into its pin - the expression is left stored, just
   // not evaluated, so re-patching the cable away brings it straight back.
   void SetExpression(int nodeIndex, int paramIndex, const std::string& expr) { mExpressions[Key(nodeIndex, paramIndex)] = expr; }
   void ClearExpression(int nodeIndex, int paramIndex) { mExpressions.erase(Key(nodeIndex, paramIndex)); mExpressionErrors.erase(Key(nodeIndex, paramIndex)); }
   const std::string* ExpressionFor(int nodeIndex, int paramIndex) const;
   bool HasExpression(int nodeIndex, int paramIndex) const { return ExpressionFor(nodeIndex, paramIndex) != nullptr; }
   const std::map<Key, std::string>& Expressions() const { return mExpressions; }

   // Set by the per-frame evaluation pass when an expression fails to parse
   // or evaluate, so the UI can surface it instead of just freezing silently.
   void SetExpressionError(int nodeIndex, int paramIndex, const std::string& error);
   const std::string* ExpressionErrorFor(int nodeIndex, int paramIndex) const;

   // Dropping the whole graph must drop the bindings with it. Node indices
   // restart from 1 on a new patch, so a link left over from the previous one
   // does not go stale - it silently re-attaches to whichever node happens to
   // land on that index next.
   void Clear() { mLinks.clear(); mExpressions.clear(); mExpressionErrors.clear(); }

   // Parameters registered during the current frame's node drawing.
   void ClearFrameParams() { mFrameParams.clear(); }
   void RegisterParam(const ParamRef& ref) { mFrameParams.push_back(ref); }
   const std::vector<ParamRef>& FrameParams() const { return mFrameParams; }

private:
   std::map<Key, Source> mLinks;
   std::map<Key, std::string> mExpressions;
   std::map<Key, std::string> mExpressionErrors;
   std::vector<ParamRef> mFrameParams;
};
