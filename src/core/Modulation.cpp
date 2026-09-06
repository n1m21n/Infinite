#include "Modulation.h"

Modulation& Modulation::Instance()
{
   static Modulation instance;
   return instance;
}

void Modulation::Bind(int nodeIndex, int paramIndex, int modulatorNodeIndex, int outputIndex)
{
   Source source;
   source.nodeIndex = modulatorNodeIndex;
   source.outputIndex = outputIndex;
   // Capture the pre-modulation value and the destination's declared range
   // now, while it's still whatever the knob was showing: every real call
   // site (drag-connect in main.cpp, the self-test fixtures) invokes Bind()
   // from within the same frame that registered this param via
   // RegisterParam, so FrameParams already has it.
   for (const ParamRef& ref : mFrameParams)
   {
      if (ref.nodeIndex == nodeIndex && ref.paramIndex == paramIndex && ref.value != nullptr)
      {
         source.centre = *ref.value;
         // Full declared span, i.e. today's override behaviour - see the
         // Source::hasRange comment for why this is the deliberately
         // conservative default rather than swinging from centre.
         source.lo = ref.minValue;
         source.hi = ref.maxValue;
         source.hasRange = true;
         break;
      }
   }
   mLinks[Key(nodeIndex, paramIndex)] = source;
}

void Modulation::RestoreLink(int nodeIndex, int paramIndex, const Source& source)
{
   mLinks[Key(nodeIndex, paramIndex)] = source;
}

void Modulation::SetRange(int nodeIndex, int paramIndex, float lo, float hi)
{
   auto it = mLinks.find(Key(nodeIndex, paramIndex));
   if (it == mLinks.end())
      return;
   it->second.lo = lo;
   it->second.hi = hi;
   it->second.hasRange = true;
}

Modulation::Source Modulation::ResolvedSourceFor(const ParamRef& ref)
{
   auto it = mLinks.find(Key(ref.nodeIndex, ref.paramIndex));
   if (it == mLinks.end())
      return Source();
   Source& s = it->second;
   if (!s.hasRange)
   {
      // First time this (necessarily just-loaded) binding's destination has
      // actually drawn a frame, so ref's declared min/max are finally known -
      // derive the equivalent lo/hi from the legacy fields exactly once.
      if (s.polarity == Source::kBipolar)
      {
         const float span = s.depth * (ref.maxValue - ref.minValue);
         s.lo = s.centre - span;
         s.hi = s.centre + span;
      }
      else
      {
         s.lo = ref.minValue;
         s.hi = ref.maxValue;
      }
      s.hasRange = true;
   }
   return s;
}

void Modulation::SetEnabled(int nodeIndex, int paramIndex, bool on)
{
   auto it = mLinks.find(Key(nodeIndex, paramIndex));
   if (it == mLinks.end())
      return;
   it->second.enabled = on;
}

void Modulation::Unbind(int nodeIndex, int paramIndex)
{
   const Key key(nodeIndex, paramIndex);
   auto it = mLinks.find(key);
   // Legacy bipolar bindings left the knob live and meaningful, so unbinding
   // restored it to where the user left it rather than freezing at whatever
   // the modulator happened to be at. A fresh binding always defaults its
   // range to the destination's full span (see Bind()) and its polarity
   // stays kAbsolute - the new lo/hi UI (SetRange) never touches polarity -
   // so this remains exactly the old kAbsolute behaviour for every binding
   // made since this field existed: the value just stays wherever the
   // modulator last wrote it, since there is no single "knob position" a
   // freely-ranged binding was swinging around.
   if (it != mLinks.end() && it->second.polarity == Source::kBipolar)
   {
      for (const ParamRef& ref : mFrameParams)
      {
         if (ref.nodeIndex == nodeIndex && ref.paramIndex == paramIndex && ref.value != nullptr)
         {
            *ref.value = it->second.centre;
            break;
         }
      }
   }
   mLinks.erase(key);
}

void Modulation::UnbindAllFor(int nodeIndex)
{
   for (auto it = mLinks.begin(); it != mLinks.end();)
   {
      if (it->first.first == nodeIndex || it->second.nodeIndex == nodeIndex)
         it = mLinks.erase(it);
      else
         ++it;
   }
   // Expressions only have a destination, not a source node, so they just
   // need dropping when the node that owned the field is gone.
   for (auto it = mExpressions.begin(); it != mExpressions.end();)
   {
      if (it->first.first == nodeIndex)
         it = mExpressions.erase(it);
      else
         ++it;
   }
   for (auto it = mExpressionErrors.begin(); it != mExpressionErrors.end();)
   {
      if (it->first.first == nodeIndex)
         it = mExpressionErrors.erase(it);
      else
         ++it;
   }
}

Modulation::Source Modulation::ModulatorFor(int nodeIndex, int paramIndex) const
{
   auto it = mLinks.find(Key(nodeIndex, paramIndex));
   return it != mLinks.end() ? it->second : Source();
}

void Modulation::RegisterParam(const ParamRef& ref)
{
   // Both stores used to take a full copy of the ParamRef every frame,
   // enumOptions included. That was free while only floats registered (their
   // option list is always empty), but a dropdown carries its whole option
   // list - a rate-division or note-name list is dozens of std::strings - so
   // registering dropdowns turned every frame into hundreds of string
   // allocations per modulatable selector, which is what made dragging a
   // macro into a mode feel sticky next to dragging it into a knob. The
   // frame list never reads enumOptions (only the apply loop uses it, and
   // only for min/max/step), and the sticky store only needs them re-copied
   // when the list actually changes.
   ParamRef& frame = mFrameParams.emplace_back();
   frame.nodeIndex = ref.nodeIndex;
   frame.paramIndex = ref.paramIndex;
   frame.value = ref.value;
   frame.minValue = ref.minValue;
   frame.maxValue = ref.maxValue;
   frame.step = ref.step;
   frame.name = ref.name;
   frame.isEnum = ref.isEnum;
   frame.isBool = ref.isBool;

   ParamRef& known = mKnownParams[Key(ref.nodeIndex, ref.paramIndex)];
   known.nodeIndex = ref.nodeIndex;
   known.paramIndex = ref.paramIndex;
   known.value = nullptr; // never valid outside the frame that registered it
   known.minValue = ref.minValue;
   known.maxValue = ref.maxValue;
   known.step = ref.step;
   known.isEnum = ref.isEnum;
   known.isBool = ref.isBool;
   if (known.name != ref.name)
      known.name = ref.name;
   // An empty incoming list means "unchanged" (the caller skipped the copy),
   // not "this param lost its options".
   if (!ref.enumOptions.empty() && known.enumOptions != ref.enumOptions)
      known.enumOptions = ref.enumOptions;
}

const ParamRef* Modulation::KnownParam(int nodeIndex, int paramIndex) const
{
   auto it = mKnownParams.find(Key(nodeIndex, paramIndex));
   return it != mKnownParams.end() ? &it->second : nullptr;
}

const std::string* Modulation::ExpressionFor(int nodeIndex, int paramIndex) const
{
   auto it = mExpressions.find(Key(nodeIndex, paramIndex));
   return it != mExpressions.end() ? &it->second : nullptr;
}

void Modulation::SetExpressionError(int nodeIndex, int paramIndex, const std::string& error)
{
   const Key key(nodeIndex, paramIndex);
   if (error.empty())
      mExpressionErrors.erase(key);
   else
      mExpressionErrors[key] = error;
}

const std::string* Modulation::ExpressionErrorFor(int nodeIndex, int paramIndex) const
{
   auto it = mExpressionErrors.find(Key(nodeIndex, paramIndex));
   return it != mExpressionErrors.end() ? &it->second : nullptr;
}
