#pragma once

#include <string>
#include <vector>

#include "Geometry3DNodes.h"
#include "Modulation.h"

// Moves something along a path over time.
//
// It emits position as three ordinary modulator outputs rather than inventing a
// transform cable, which means it patches into any parameter at all - the pos
// x/y/z of a Geometry node for literal motion, but equally a blur radius or a
// hue, so the same curve can drive things that have nothing to do with space.
//
// Outputs are normalised 0..1 like every other modulator, and each destination
// maps that onto its own range. Position is therefore emitted as 0..1 across
// the path's bounding extent, not in scene units.
class PathNode : public INode, public IModulator
{
public:
   enum Shape
   {
      kCircle = 0, kLine, kFigureEight, kHelix, kSpiral, kLissajous, kShapeCount
   };

   // How a patched mesh is turned into something to travel along. A boundary is
   // the outline of an open surface; a closed object has none, so slicing it
   // gives the contour that "around the object" actually means there.
   enum FollowMode { kFollowBoundary = 0, kFollowSlice, kFollowModeCount };
   static const std::vector<std::string>& FollowModeNames();

   static INode* Create() { return new PathNode(); }
   static const std::vector<std::string>& ShapeNames();

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   // A patched curve or mesh replaces the parametric shape entirely: it is the
   // more specific instruction. Curve wins over geometry when both are
   // present. Read via GetCurve() - a plain mesh source with no curve of its
   // own returns nullptr, same as an unconnected pin.
   IGeometrySource* curveSource = nullptr;
   IGeometrySource* geometrySource = nullptr;
   IGeometrySource** GeometryInputSlot(int slot) override
   {
      if (slot == 0) return &curveSource;
      if (slot == 1) return &geometrySource;
      return nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "curve" : "geo";
   }

   int followMode = kFollowSlice;
   float slicePosition = 0.0f;
   int sliceAxis = 1;
   int contourIndex = 0;
   bool alignToPath = true;   // reserved: orientation output
   size_t FollowPointCount() const { return mFollow.Count(); }
   bool IsFollowing() const { return !mFollow.Empty(); }

   // X, Y, Z and the raw progress along the path.
   int OutputCount() const override { return 4; }
   const char* OutputLabel(int index) const override
   {
      static const char* kNames[] = { "x", "y", "z", "t" };
      return (index >= 0 && index < 4) ? kNames[index] : "out";
   }

   float Value01() override; // X
   IModulator* ModulatorOutput(int index) override;

   // Where on the path the point currently is, in path units (-1..1-ish),
   // for the node's own preview.
   void CurrentPoint(float out[3]) const;
   float Progress() const { return mProgress; }

   int shape = kCircle;
   float speed = 0.25f;   // turns per beat
   float phase = 0.0f;
   float sizeX = 1.0f;
   float sizeY = 1.0f;
   float sizeZ = 1.0f;
   float turns = 3.0f;    // helix and spiral
   int lissajousA = 3;
   int lissajousB = 2;
   bool pingPong = false; // reverse at the ends instead of jumping back

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shape", shape); v.Float("speed", speed); v.Float("phase", phase);
      v.Float("sizeX", sizeX); v.Float("sizeY", sizeY); v.Float("sizeZ", sizeZ);
      v.Float("turns", turns); v.Int("lissajousA", lissajousA);
      v.Int("lissajousB", lissajousB); v.Bool("pingPong", pingPong);
      v.Int("followMode", followMode); v.Float("slicePosition", slicePosition);
      v.Int("sliceAxis", sliceAxis); v.Int("contourIndex", contourIndex);
   }

private:
   // Sibling outputs. Each just reads the axis its parent already computed, so
   // the path is evaluated once per frame however many outputs are patched.
   class AxisOutput : public IModulator
   {
   public:
      PathNode* owner = nullptr;
      int axis = 0; // 0..2 position, 3 progress
      float Value01() override;
   };

   void Evaluate();
   void RebuildFollowIfNeeded();

   // The polyline actually being travelled when a curve or mesh is patched in.
   Polyline mFollow;
   const void* mBuiltCurve = nullptr;
   const void* mBuiltGeometry = nullptr;
   unsigned long long mBuiltRevision = 0;
   int mBuiltFollowMode = -1, mBuiltSliceAxis = -1, mBuiltContour = -1;
   float mBuiltSlicePosition = -999.0f;
   // The followed contour is extracted in world space (see
   // RebuildFollowIfNeeded), and GetModelMatrix() is a live value with no
   // revision of its own, so it must be compared directly to catch a pure
   // transform edit on the geometry source.
   Mat4 mBuiltGeometryModel;

   float mPoint[3] = { 0.0f, 0.0f, 0.0f };
   float mProgress = 0.0f;
   int mLastCookFrame = -1;

   AxisOutput mY, mZ, mT;
   bool mOutputsBound = false;
};
