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
}

Modulation::Source Modulation::ModulatorFor(int nodeIndex, int paramIndex) const
{
   auto it = mLinks.find(Key(nodeIndex, paramIndex));
   return it != mLinks.end() ? it->second : Source();
}
