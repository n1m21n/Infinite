#include "Palette.h"

PaletteBinding& PaletteBinding::Instance()
{
   static PaletteBinding instance;
   return instance;
}

void PaletteBinding::Bind(int nodeIndex, int colorIndex, int paletteNodeIndex, int swatchIndex)
{
   Source source;
   source.nodeIndex = paletteNodeIndex;
   source.swatchIndex = swatchIndex;
   mLinks[Key(nodeIndex, colorIndex)] = source;
}

void PaletteBinding::Unbind(int nodeIndex, int colorIndex)
{
   mLinks.erase(Key(nodeIndex, colorIndex));
}

void PaletteBinding::UnbindAllFor(int nodeIndex)
{
   for (auto it = mLinks.begin(); it != mLinks.end();)
   {
      if (it->first.first == nodeIndex || it->second.nodeIndex == nodeIndex)
         it = mLinks.erase(it);
      else
         ++it;
   }
}

PaletteBinding::Source PaletteBinding::SourceFor(int nodeIndex, int colorIndex) const
{
   auto it = mLinks.find(Key(nodeIndex, colorIndex));
   return it != mLinks.end() ? it->second : Source();
}

int PaletteBinding::BindingCountFrom(int paletteNodeIndex, int targetNodeIndex) const
{
   int count = 0;
   for (const auto& link : mLinks)
   {
      if (link.first.first == targetNodeIndex && link.second.nodeIndex == paletteNodeIndex)
         count++;
   }
   return count;
}
