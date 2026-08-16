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
   // Capture the pre-modulation value now, while it's still whatever the
   // knob was showing: every real call site (drag-connect in main.cpp, the
   // self-test fixtures) invokes Bind() from within the same frame that
   // registered this param via RegisterParam, so FrameParams already has it.
   for (const ParamRef& ref : mFrameParams)
   {
      if (ref.nodeIndex == nodeIndex && ref.paramIndex == paramIndex && ref.value != nullptr)
      {
         source.centre = *ref.value;
         break;
      }
   }
   mLinks[Key(nodeIndex, paramIndex)] = source;
}

void Modulation::RestoreLink(int nodeIndex, int paramIndex, const Source& source)
{
   mLinks[Key(nodeIndex, paramIndex)] = source;
}

void Modulation::SetPolarity(int nodeIndex, int paramIndex, int polarity, float depth)
{
   auto it = mLinks.find(Key(nodeIndex, paramIndex));
   if (it == mLinks.end())
      return;
   it->second.polarity = polarity;
   it->second.depth = depth;
}

void Modulation::Unbind(int nodeIndex, int paramIndex)
{
   const Key key(nodeIndex, paramIndex);
   auto it = mLinks.find(key);
   // Bipolar mode leaves the knob live and meaningful, so unbinding restores
   // it to where the user left it rather than freezing at whatever the
   // modulator happened to be at. Absolute mode keeps its long-standing
   // behaviour (the value just stays wherever the modulator last wrote it) -
   // there is no "knob position" to return to when the binding overrode the
   // parameter outright.
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
