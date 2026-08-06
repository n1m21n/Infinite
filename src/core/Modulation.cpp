#include "Modulation.h"

Modulation& Modulation::Instance()
{
   static Modulation instance;
   return instance;
}

void Modulation::Bind(int nodeIndex, int paramIndex, int modulatorNodeIndex)
{
   mLinks[Key(nodeIndex, paramIndex)] = modulatorNodeIndex;
}

void Modulation::Unbind(int nodeIndex, int paramIndex)
{
   mLinks.erase(Key(nodeIndex, paramIndex));
}

void Modulation::UnbindAllFor(int nodeIndex)
{
   for (auto it = mLinks.begin(); it != mLinks.end();)
   {
      if (it->first.first == nodeIndex || it->second == nodeIndex)
         it = mLinks.erase(it);
      else
         ++it;
   }
}

int Modulation::ModulatorFor(int nodeIndex, int paramIndex) const
{
   auto it = mLinks.find(Key(nodeIndex, paramIndex));
   return it != mLinks.end() ? it->second : -1;
}
