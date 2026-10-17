#pragma once

#include <array>
#include <string>
#include <vector>

#include "INode.h"
#include "ImageCable.h"
#include "GLUtil.h"
#include "FilterDefs.h"

// Single-input, single-output shader-pass node. One class serves every entry
// in the FilterDefs table (Effects + Color/Compositing) - the whole point of
// the declarative FilterDef approach: dozens of Affinity-style modules, each
// spawnable as its own node with its own params, without a class per effect.
class FilterNode : public INode
{
public:
   // `def` must outlive the node - defs live in the static vector returned by GetFilterDefs().
   static INode* CreateFor(const FilterDef& def) { return new FilterNode(def); }

   // Turbo 0.59 (upstream's warm-up): builds one effect's shaders ahead of
   // use, so the first time an effect is added it links from the in-memory
   // cache instead of compiling in that frame. Main thread, GL current; call
   // once a frame. Returns false once every effect has been warmed.
   static bool WarmNextShader();

   explicit FilterNode(const FilterDef& def);
   ~FilterNode() override;

   unsigned int GetOutputTexture() override { return GLUtil::FboTexture(mOut); }
   int GetOutputWidth() const override { return mOut.w; }
   int GetOutputHeight() const override { return mOut.h; }
   unsigned int GetOutputFormat() const override { return mOut.internalFormat; }
   void CookIfNeeded(int frameId) override;
   unsigned long long TextureRevision() const override { return mRevision; }

   ImageCable& Input() { return mInput; }
   INode* BypassSource() override { return mInput.GetSource(); }
   ImageCable& Input2() { return mInput2; }
   const FilterDef& Def() const { return mDef; }

   // component in [0,2] for Color params, 0 for Float/Int/Bool.
   float GetParamValue(size_t paramIndex, int component = 0) const { return mParamValues[paramIndex][component]; }
   void SetParamValue(size_t paramIndex, int component, float value) { mParamValues[paramIndex][component] = value; }

   // Direct handle for widgets that edit in place (colour picker).
   float* ParamPtr(size_t paramIndex) { return mParamValues[paramIndex].data(); }

   // Keyed by uniformName rather than by index: the FilterDef table is shared
   // across every FilterNode of this kind, so the name is the one thing stable
   // enough to survive across app versions if params are reordered.
   void VisitParams(ParamVisitor& v) override
   {
      for (size_t i = 0; i < mDef.params.size(); i++)
      {
         const FilterParamDef& def = mDef.params[i];
         if (def.type == FilterParamDef::Type::Color)
            v.Color(def.uniformName.c_str(), mParamValues[i].data());
         else
            v.Float(def.uniformName.c_str(), mParamValues[i][0]);
      }
      common.Visit(v);
   }

   // Turbo (TOP parity): TouchDesigner's "Common" page, shared by every
   // FilterDef. Every default reproduces what a filter did before these
   // existed (input size, 8-bit, the def's own edge rule, linear, one pass,
   // all channels), so older patches render unchanged.
   struct Common
   {
      enum Res { kResInput, kResHalf, kResQuarter, kResEighth, kRes2x, kRes4x, kResCustom, kResFit, kResLimit };
      enum Format { kFmt8, kFmt16f, kFmt32f, kFmtInput };

      int res = kResInput;
      int width = 1280; // Custom / Fit / Limit
      int height = 720;
      int format = kFmt8;
      int extend = 0; // 0 = the def's defaultExtend, else GLUtil::Extend + 1
      int smooth = 0; // GLUtil::Filter
      int passes = 1;
      bool chanR = true, chanG = true, chanB = true, chanA = true;

      void Visit(ParamVisitor& v)
      {
         v.Int("cmnRes", res); v.Int("cmnW", width); v.Int("cmnH", height);
         v.Int("cmnFormat", format); v.Int("cmnExtend", extend); v.Int("cmnSmooth", smooth);
         v.Int("cmnPasses", passes);
         v.Bool("cmnR", chanR); v.Bool("cmnG", chanG); v.Bool("cmnB", chanB); v.Bool("cmnA", chanA);
      }
      bool AllChannels() const { return chanR && chanG && chanB && chanA; }

      static const std::vector<std::string>& ResNames();
      static const std::vector<std::string>& FormatNames();
      static const std::vector<std::string>& ExtendNames();
      static const std::vector<std::string>& SmoothNames();
   };
   Common common;

   // The size this node renders at for an input of inW x inH (its natural
   // size - see FilterDef::naturalSize - put through Output Resolution).
   void OutputSizeFor(int inW, int inH, int& outW, int& outH) const;
   int EffectiveExtend() const { return common.extend > 0 ? common.extend - 1 : mDef.defaultExtend; }

private:
   // Uniform locations are fixed for the life of a linked program, so they're
   // looked up once in EnsureShader() rather than every cook - glGetUniformLocation
   // is a name-hash lookup into the driver, not free, and every FilterNode pays it
   // every uncached cook otherwise.
   struct PassLocs
   {
      GLint src = -1;
      GLint src2 = -1;
      GLint hasSrc2 = -1;
      GLint pass = -1;
      GLint texel = -1;
      GLint time = -1;
      GLint beats = -1;
      GLint clock = -1;
      GLint extend = -1;
      GLint maskOn = -1;
      GLint mask = -1;
      std::vector<GLint> params;
   };

   bool EnsureShader();
   void LookupLocs(unsigned int program, PassLocs& locs) const;
   void BindUniforms(const PassLocs& locs, unsigned int srcTex, unsigned int srcTex2,
                     unsigned int passTex, int srcW, int srcH,
                     float time, float beats, float clock) const;

   // Everything the shader pass's output depends on. Reusing mOut's contents
   // is only safe when all of this is identical to the last time it ran -
   // mirrors GeometryOpNode::Signature (GeometryOpNodes.cpp).
   struct Signature
   {
      unsigned long long upstreamRev = 0;
      unsigned long long upstreamRev2 = 0;
      int width = 0;
      int height = 0;
      std::vector<std::array<float, 3>> params;
      // The exact uTime value uploaded, 0 for filters that don't read it.
      // Transport time only advances while playing, so a time-driven filter
      // on a stopped transport caches like any other instead of re-rendering
      // an identical image and forcing everything downstream to recook.
      float time = 0.0f;
      // Turbo 0.51: uBeats / uClock as uploaded (0 when unused). The wall clock
      // is only part of the signature in free-run mode.
      float beats = 0.0f;
      float clock = 0.0f;
      // Turbo (TOP parity): output size/format and the Common page.
      int outW = 0;
      int outH = 0;
      unsigned int format = 0;
      ParamSnapshot common;

      bool operator==(const Signature& o) const
      {
         return upstreamRev == o.upstreamRev && upstreamRev2 == o.upstreamRev2 &&
                width == o.width && height == o.height && params == o.params &&
                time == o.time && beats == o.beats && clock == o.clock &&
                outW == o.outW && outH == o.outH && format == o.format && common == o.common;
      }
   };

   const FilterDef& mDef;
   std::vector<std::array<float, 3>> mParamValues;

   ImageCable mInput;
   ImageCable mInput2; // only used when Def().inputs == 2
   GLUtil::Fbo mOut;
   GLUtil::Fbo mPassFbo[2]; // Passes > 1: ping-pong between these, last pass into mOut
   unsigned int mProgram = 0;
   unsigned int mPreProgram = 0;
   bool mShaderTried = false;
   int mLastCookFrame = -1;

   PassLocs mMainLocs;
   PassLocs mPreLocs;

   // Filters whose shader reads uTime are inherently animated, so the
   // uploaded time value is part of Signature.
   bool mUsesTime = false;
   bool mUsesClock = false;
   int mClockModeParam = -1; // index of the "uClockMode" param, if the def has one
   bool mHasBuilt = false;
   Signature mBuilt;
   unsigned long long mRevision = 0;
};
