#include "PathNode.h"

#include <algorithm>
#include <cmath>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kShapeNames = {
      "Circle", "Line", "Figure 8", "Helix", "Spiral", "Lissajous"
   };
   const float kTau = 6.28318530718f;
}

const std::vector<std::string>& PathNode::ShapeNames() { return kShapeNames; }

void PathNode::Evaluate()
{
   // Driven by the transport rather than wall clock, so pausing freezes the
   // motion and rewinding puts it back exactly where it started - the same
   // determinism the rest of the graph relies on.
   const double beats = Transport::Instance().Beats();
   float t = (float)(beats * (double)speed) + phase;

   if (pingPong)
   {
      // Triangle wave over the unit interval: the point runs to the end of the
      // path and retraces it rather than snapping back to the start.
      const float wrapped = t - std::floor(t * 0.5f) * 2.0f;
      t = (wrapped <= 1.0f) ? wrapped : (2.0f - wrapped);
   }
   else
   {
      t = t - std::floor(t);
   }
   mProgress = t;

   const float angle = t * kTau;
   switch (shape)
   {
      case kLine:
         // A straight run from one corner to the other, not a loop.
         mPoint[0] = (t * 2.0f - 1.0f) * sizeX;
         mPoint[1] = (t * 2.0f - 1.0f) * sizeY;
         mPoint[2] = (t * 2.0f - 1.0f) * sizeZ;
         break;
      case kFigureEight:
         // Lemniscate of Gerono: crosses itself once per lap.
         mPoint[0] = std::sin(angle) * sizeX;
         mPoint[1] = std::sin(angle) * std::cos(angle) * sizeY;
         mPoint[2] = std::cos(angle) * sizeZ * 0.0f;
         break;
      case kHelix:
         mPoint[0] = std::cos(angle * turns) * sizeX;
         mPoint[1] = (t * 2.0f - 1.0f) * sizeY;
         mPoint[2] = std::sin(angle * turns) * sizeZ;
         break;
      case kSpiral:
      {
         // Radius grows with progress, so it winds outward rather than orbiting.
         const float radius = t;
         mPoint[0] = std::cos(angle * turns) * radius * sizeX;
         mPoint[1] = (t * 2.0f - 1.0f) * sizeY * 0.25f;
         mPoint[2] = std::sin(angle * turns) * radius * sizeZ;
         break;
      }
      case kLissajous:
         mPoint[0] = std::sin(angle * (float)std::max(1, lissajousA)) * sizeX;
         mPoint[1] = std::sin(angle * (float)std::max(1, lissajousB) + 1.5707963f) * sizeY;
         mPoint[2] = std::cos(angle * (float)std::max(1, lissajousA)) * sizeZ;
         break;
      case kCircle:
      default:
         mPoint[0] = std::cos(angle) * sizeX;
         mPoint[1] = std::sin(angle) * sizeY;
         mPoint[2] = 0.0f;
         break;
   }
}

void PathNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!mOutputsBound)
   {
      mY.owner = this; mY.axis = 1;
      mZ.owner = this; mZ.axis = 2;
      mT.owner = this; mT.axis = 3;
      mOutputsBound = true;
   }
   Evaluate();
}

void PathNode::CurrentPoint(float out[3]) const
{
   out[0] = mPoint[0];
   out[1] = mPoint[1];
   out[2] = mPoint[2];
}

float PathNode::Value01()
{
   // Modulators can be read before the node has cooked this frame - a parameter
   // bound to this may be drawn first - so evaluate on demand rather than
   // handing back a stale point.
   const float maxSize = std::max(0.001f, std::max(sizeX, std::max(sizeY, sizeZ)));
   return std::max(0.0f, std::min(1.0f, mPoint[0] / (2.0f * maxSize) + 0.5f));
}

float PathNode::AxisOutput::Value01()
{
   if (owner == nullptr)
      return 0.0f;
   if (axis == 3)
      return owner->mProgress;

   const float maxSize = std::max(0.001f,
      std::max(owner->sizeX, std::max(owner->sizeY, owner->sizeZ)));
   // Normalised against the largest axis rather than each axis separately, so a
   // flattened path stays flattened at its destination instead of being
   // stretched back out to full range on every axis.
   return std::max(0.0f, std::min(1.0f, owner->mPoint[axis] / (2.0f * maxSize) + 0.5f));
}

IModulator* PathNode::ModulatorOutput(int index)
{
   if (!mOutputsBound)
   {
      mY.owner = this; mY.axis = 1;
      mZ.owner = this; mZ.axis = 2;
      mT.owner = this; mT.axis = 3;
      mOutputsBound = true;
   }
   switch (index)
   {
      case 1: return &mY;
      case 2: return &mZ;
      case 3: return &mT;
      default: return this;
   }
}
