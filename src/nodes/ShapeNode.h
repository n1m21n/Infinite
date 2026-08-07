#pragma once

#include <string>
#include <vector>

#include "INode.h"
#include "GLUtil.h"

// Procedural shape generator (Source category). SDF-based so fill, stroke and
// feather all fall out of one distance value, and every shape kind shares one
// fragment shader with an int switch - same "one class, many variants" approach
// FilterNode uses for the effects table.
class ShapeNode : public INode
{
public:
   static INode* Create() { return new ShapeNode(); }
   static const std::vector<std::string>& ShapeNames();

   ~ShapeNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   void CookIfNeeded(int frameId) override;

   // Public so the ImGui params panel can bind widgets straight to them.
   int shapeType = 0; // index into ShapeNames()
   float width = 1024.0f;
   float height = 1024.0f;
   float size = 0.35f;
   float aspect = 1.0f;
   float cornerRadius = 0.05f;
   int sides = 5; // polygon/star
   float innerRatio = 0.5f; // star
   float rotation = 0.0f;
   float posX = 0.5f;
   float posY = 0.5f;
   float fillColor[3] = { 1.0f, 1.0f, 1.0f };
   float fillOpacity = 1.0f;
   float strokeWidth = 0.0f;
   float strokeColor[3] = { 0.0f, 0.0f, 0.0f };
   float feather = 0.005f;
   float bgColor[3] = { 0.0f, 0.0f, 0.0f };
   float bgOpacity = 0.0f;

   void VisitParams(ParamVisitor& v) override
   {
      v.Int("shapeType", shapeType);
      v.Float("width", width); v.Float("height", height);
      v.Float("size", size); v.Float("aspect", aspect);
      v.Float("cornerRadius", cornerRadius); v.Int("sides", sides);
      v.Float("innerRatio", innerRatio); v.Float("rotation", rotation);
      v.Float("posX", posX); v.Float("posY", posY);
      v.Color("fillColor", fillColor); v.Float("fillOpacity", fillOpacity);
      v.Float("strokeWidth", strokeWidth); v.Color("strokeColor", strokeColor);
      v.Float("feather", feather);
      v.Color("bgColor", bgColor); v.Float("bgOpacity", bgOpacity);
   }

private:
   bool EnsureShader();

   GLUtil::Fbo mOut;
   unsigned int mProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;
};
