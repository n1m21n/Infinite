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
   // Grid the destination snaps to before clamping - 0 means continuous,
   // 1 means an integer destination (ModSliderInt/ModKnobInt). See
   // ShapeToParam in main.cpp's modulation apply loop.
   float step = 0.0f;
   std::string name;
   bool isEnum = false;
   bool isBool = false;
   std::vector<std::string> enumOptions;
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
      // Legacy polarity concept - a binding used to be either an absolute
      // override (kAbsolute) or a swing around a captured centre (kBipolar).
      // Both are now just special cases of the lo/hi range below and the
      // apply loop no longer branches on this; it is kept, alongside
      // depth/centre, purely so a patch saved before lo/hi existed decodes
      // to the identical range it swung before - see RestoreLink/EnsureRange
      // in the .cpp. kAbsolute (0) is the default read for a "mod" line
      // with no polarity token at all, matching pre-existing patch files.
      enum Polarity { kAbsolute = 0, kBipolar };

      int nodeIndex = -1;
      int outputIndex = 0;
      int polarity = kAbsolute;
      // Only meaningful for the legacy kBipolar conversion. -1..1, matching
      // ModDepthNode's convention (negative inverts).
      float depth = 1.0f;
      // The destination parameter's value at the instant the binding was
      // created (or, for a patch load, whatever was saved). Only meaningful
      // for the legacy conversion now - see EnsureRange.
      float centre = 0.0f;

      // The range this binding writes into the destination, in the
      // destination parameter's own units: the apply loop computes
      // lo + (hi - lo) * clamp(v01, 0, 1), then snaps/clamps that to the
      // destination's own grid (ShapeToParam in main.cpp). This is the
      // single thing the apply loop reads to know what to write.
      float lo = 0.0f;
      float hi = 0.0f;
      // False only for a Source freshly decoded from a "mod" line that had
      // no lo/hi tokens (an old patch) - EnsureRange derives lo/hi from
      // polarity/depth/centre the first time this binding's destination
      // param is actually drawn (it can't happen at load time: the
      // destination hasn't rendered a frame yet, so its declared min/max
      // aren't known - see RestoreLink) and flips this true. Every other
      // Source (freshly bound, or already converted) has it true.
      bool hasRange = false;

      // The binding still exists and still shows in the matrix, it just
      // stops being written by the apply loop, leaving the destination
      // param at whatever value it last held. The param field stays
      // read-only-locked, because the cable is still patched - matching
      // how HasExpression behaves under a live cable.
      bool enabled = true;
   };

   // Creates a fresh binding, defaulting the range to the destination's full
   // declared span (today's override behaviour), and capturing the
   // destination's current value as `centre` for legacy compatibility -
   // both read through this frame's FrameParams (see the .cpp for why
   // that's safe at every real call site).
   void Bind(int nodeIndex, int paramIndex, int modulatorNodeIndex, int outputIndex = 0);
   // Patch load only: installs a Source exactly as read from disk. Unlike
   // Bind(), it must NOT derive anything from the current frame, since the
   // node whose value should become centre/lo/hi may not have drawn a
   // single frame yet - see EnsureRange, which does that derivation lazily
   // once the destination has actually been drawn.
   void RestoreLink(int nodeIndex, int paramIndex, const Source& source);
   // Sets an existing binding's lo/hi directly, in destination units -
   // leaves nodeIndex/outputIndex/centre/polarity/depth untouched. No-op if
   // nothing is bound there.
   void SetRange(int nodeIndex, int paramIndex, float lo, float hi);
   // Enables or disables an existing binding in place, leaving everything
   // else about it untouched. No-op if nothing is bound there, mirroring
   // SetRange().
   void SetEnabled(int nodeIndex, int paramIndex, bool on);
   void Unbind(int nodeIndex, int paramIndex);
   void UnbindAllFor(int nodeIndex); // node deleted: drop it as target and as source

   // nodeIndex is -1 when the parameter is not modulated.
   Source ModulatorFor(int nodeIndex, int paramIndex) const;
   bool IsModulated(int nodeIndex, int paramIndex) const { return ModulatorFor(nodeIndex, paramIndex).nodeIndex >= 0; }
   // Same as ModulatorFor, but first derives lo/hi from the legacy
   // polarity/depth/centre fields if this Source hasn't been converted yet
   // (see Source::hasRange) - ref supplies the destination's declared
   // min/max for that one-time conversion. This is what the apply loop
   // calls; ModulatorFor above stays a plain lookup for every other caller
   // (the binding-menu popup, the self-test fixtures) that doesn't have a
   // ParamRef in hand.
   Source ResolvedSourceFor(const ParamRef& ref);

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
   void Clear() { mLinks.clear(); mExpressions.clear(); mExpressionErrors.clear(); mKnownParams.clear(); }

   // Parameters registered during the current frame's node drawing.
   void ClearFrameParams() { mFrameParams.clear(); }
   void RegisterParam(const ParamRef& ref);
   const std::vector<ParamRef>& FrameParams() const { return mFrameParams; }

   // Sticky per-parameter metadata, accumulated from RegisterParam and never
   // cleared per-frame. The matrix panel needs a destination's name/min/max/
   // step even on frames where that param didn't draw - a top/left-docked
   // panel draws before the canvas has registered anything at all this frame,
   // and a param hidden behind a mode switch never draws at all. (A collapsed
   // node used to be on this list too; it now keeps registering its params
   // without drawing them - see gParamRegisterOnly in main.cpp - because
   // registering is what lets a modulator keep writing into it.)
   // Cleared only by Clear(). The `value` pointer is deliberately nulled in
   // the stored copy: the raw float* is only valid within the frame that
   // registered it - never store or dereference it from here.
   const ParamRef* KnownParam(int nodeIndex, int paramIndex) const;
   const std::map<Key, ParamRef>& AllKnownParams() const { return mKnownParams; }

private:
   std::map<Key, Source> mLinks;
   std::map<Key, std::string> mExpressions;
   std::map<Key, std::string> mExpressionErrors;
   std::vector<ParamRef> mFrameParams;
   std::map<Key, ParamRef> mKnownParams;
};
