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
   mLinks[Key(nodeIndex, paramIndex)] = source;
}

void Modulation::Unbind(int nodeIndex, int paramIndex)
{
   mLinks.erase(Key(nodeIndex, paramIndex));
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
