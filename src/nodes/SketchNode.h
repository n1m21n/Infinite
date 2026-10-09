#pragma once

#include <map>
#include <string>
#include <vector>

#include "INode.h"
#include "field/ParamTable.h"
#include "sketch/SketchEngine.h"

// Source node whose body is a JavaScript sketch (p5.js-flavoured API), drawn by
// quickjs-ng + plutovg on the CPU and uploaded as a texture. `param(name, def,
// lo, hi)` calls in the script become real, modulatable knobs through the same
// Field::ParamTable the Field nodes use. Design: docs/plans/sketch/README.md.
class SketchNode : public INode
{
public:
   static INode* Create() { return new SketchNode(); }
   SketchNode();
   ~SketchNode() override;

   unsigned int GetOutputTexture() override { return mTex; }
   int GetOutputWidth() const override { return mW; }
   int GetOutputHeight() const override { return mH; }
   void CookIfNeeded(int frameId) override;
   // Bumped only when draw() actually ran (README S3), so an idle sketch settles.
   unsigned long long TextureRevision() const override { return mRevision; }

   void VisitParams(ParamVisitor& v) override
   {
      v.Text("code", code);
      v.Text("svg", svg);
      v.Text("svgName", svgName);
      v.Float("width", width);
      v.Float("height", height);
      v.Bool("animate", animate);
      mParamTable.VisitParams(v);
   }

   // Compiles `code`. On failure the last working program (and picture) stay.
   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }
   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   struct Preset { const char* name; const char* code; const char* svg; };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);

   // The app sets this once at startup (bundled Inter) so text() has a font.
   static void SetFontPath(const std::string& path);

   std::string code;
   std::string svg;       // optional SVG source, saved with the patch so it stays portable
   std::string svgName;   // file name it came from, for display only

   // Reads an .svg file into `svg` (and, if the script is still the default, swaps in the
   // SVG starter so the dropped file animates straight away). False + LastError on failure.
   bool LoadSvgFile(const std::string& path);
   float width = 1024.0f;
   float height = 1024.0f;
   bool animate = true;
   int presetIndex = 0;

private:
   SketchEngine mEngine;
   Field::ParamTable mParamTable;
   std::string mLastError, mNotice;
   std::string mCompiledCode, mCompiledSvg;
   bool mCompiledOnce = false;
   int mLastCookFrame = -1;
   int mNodeIndex = -1;

   unsigned int mTex = 0;
   int mW = 4, mH = 4;          // size of the uploaded texture
   int mUploadedW = 0, mUploadedH = 0;
   unsigned long long mRevision = 0;
   std::vector<uint8_t> mPixels, mFlipped;

   // What the last draw() saw; a cook with identical inputs does nothing.
   bool mHasRun = false;
   int mRunW = 0, mRunH = 0;
   double mRunT = 0;
   std::map<std::string, float> mRunParams;
};
