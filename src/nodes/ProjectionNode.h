#pragma once

#include <array>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"

// Projection Mapping & Warping node. Provides 4-corner perspective homography,
// subdivided grid mesh warping, alignment test patterns, and projector-native
// resolution targeting in a single GPU render pass.
class ProjectionNode : public INode
{
public:
   struct Point
   {
      float x = 0.0f;
      float y = 0.0f;
   };

   enum class WarpMode
   {
      CornerPin = 0,
      MeshGrid = 1
   };

   enum class TestPattern
   {
      Off = 0,
      Grid = 1,
      Crosshairs = 2,
      ColorBars = 3,
      Combined = 4
   };

   static INode* Create() { return new ProjectionNode(); }
   static const std::vector<std::string>& PatternNames();

   ProjectionNode();
   ~ProjectionNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   const char* InputLabel(int slot) const override { return slot == 0 ? "in" : nullptr; }

   // --- Parameters ---
   int mode = 0; // 0 = CornerPin, 1 = MeshGrid
   float width = 1920.0f;
   float height = 1080.0f;
   bool matchInput = false;
   int patternMode = 0; // 0 = Off, 1 = Grid, 2 = Crosshairs, 3 = ColorBars, 4 = Combined

   int gridW = 2;
   int gridH = 2;
   Point points[8][8];

   void SetGridSize(int newW, int newH);
   void ResetCorners();
   void ResetAllPoints();
   void FlipH();
   void FlipV();

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode);
      v.Float("width", width);
      v.Float("height", height);
      v.Bool("matchInput", matchInput);
      v.Int("patternMode", patternMode);
      v.Int("gridW", gridW);
      v.Int("gridH", gridH);
      for (int r = 0; r < 8; ++r)
      {
         for (int c = 0; c < 8; ++c)
         {
            char keyX[32], keyY[32];
            snprintf(keyX, sizeof(keyX), "p_r%d_c%d_x", r, c);
            snprintf(keyY, sizeof(keyY), "p_r%d_c%d_y", r, c);
            v.Float(keyX, points[r][c].x);
            v.Float(keyY, points[r][c].y);
         }
      }
   }

private:
   bool EnsureShader();
   void EnsureMesh();
   void UpdateMeshVertices();

   struct Signature
   {
      unsigned long long upstreamRev = 0;
      int width = 0;
      int height = 0;
      bool matchInput = false;
      int mode = 0;
      int patternMode = 0;
      int gridW = 0;
      int gridH = 0;
      bool hasInput = false;
      std::array<float, 128> points{};

      bool operator==(const Signature& o) const
      {
         return upstreamRev == o.upstreamRev &&
                width == o.width && height == o.height && matchInput == o.matchInput &&
                mode == o.mode && patternMode == o.patternMode &&
                gridW == o.gridW && gridH == o.gridH && hasInput == o.hasInput &&
                points == o.points;
      }
   };

   ImageCable mInput;
   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   unsigned int mVao = 0;
   unsigned int mVbo = 0;
   unsigned int mEbo = 0;
   int mIndexCount = 0;

   bool mHasBuilt = false;
   Signature mBuilt;
   unsigned long long mRevision = 0;
};
