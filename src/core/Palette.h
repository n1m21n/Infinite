#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

// Colour bindings: the same idea as Modulation, one layer over.
//
// A modulator speaks in 0..1 and a binding maps that onto a slider's range.
// That covers every scalar in the graph but stops dead at colour, which is the
// one parameter type you most want to drive from somewhere else - a patch's
// look lives in twenty scattered swatches (ramp stops, material albedo, curve
// low/high, text fill) and there is no way to restate all of them at once.
//
// So colours get their own binding table, keyed the same way: (node, colour
// slot) -> (palette node, swatch). Deliberately not folded into Modulation:
// a colour is three numbers with no meaningful range mapping, so pushing it
// through Value01() would mean three bindings per swatch and a normalisation
// that means nothing. Keeping the tables separate also means colour pins get
// their own id block, so existing patches' parameter indices do not shift.
class IPaletteSource
{
public:
   virtual ~IPaletteSource() {}

   // How many swatches this source currently offers. Bindings past the end are
   // wrapped rather than dropped, so lowering the count never loses a link.
   virtual int SwatchCount() const = 0;

   // Writes swatch `index` as linear RGB into outRgb[3].
   virtual void GetSwatch(int index, float outRgb[3]) const = 0;
};

// One colour parameter, re-registered every frame while its node draws - the
// float* is only ever used within the frame that registered it, exactly like
// ParamRef.
struct ColorRef
{
   int nodeIndex = 0;
   int colorIndex = 0;
   float* value = nullptr;
   std::string name;
};

class PaletteBinding
{
public:
   using Key = std::pair<int, int>; // (node index, colour index)

   struct Source
   {
      int nodeIndex = -1;
      int swatchIndex = 0;
   };

   static PaletteBinding& Instance();

   void Bind(int nodeIndex, int colorIndex, int paletteNodeIndex, int swatchIndex);
   void Unbind(int nodeIndex, int colorIndex);
   void UnbindAllFor(int nodeIndex); // node deleted: drop it as target and as source

   // nodeIndex is -1 when the colour is not bound.
   Source SourceFor(int nodeIndex, int colorIndex) const;
   bool IsBound(int nodeIndex, int colorIndex) const { return SourceFor(nodeIndex, colorIndex).nodeIndex >= 0; }

   // How many colours on `nodeIndex` are already driven by `paletteNodeIndex`.
   // Used to pick a swatch when a new cable lands: dragging a palette onto a
   // ramp's five stops in turn should hand out swatches 0,1,2,3,4 rather than
   // five copies of the same colour.
   int BindingCountFrom(int paletteNodeIndex, int targetNodeIndex) const;

   const std::map<Key, Source>& Links() const { return mLinks; }

   // See Modulation::Clear - node indices restart from 1 on a new patch, so a
   // binding left behind would re-attach itself to an unrelated node.
   void Clear() { mLinks.clear(); }

   // Colours registered during the current frame's node drawing.
   void ClearFrameColors() { mFrameColors.clear(); }
   void RegisterColor(const ColorRef& ref) { mFrameColors.push_back(ref); }
   const std::vector<ColorRef>& FrameColors() const { return mFrameColors; }

private:
   std::map<Key, Source> mLinks;
   std::vector<ColorRef> mFrameColors;
};
