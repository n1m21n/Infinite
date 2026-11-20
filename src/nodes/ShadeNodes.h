#pragma once

// Shading nodes (OPUS_PLAN rows 2.3 to 2.6, route chosen in 2.2): small node classes whose cables carry a Field
// expression, not pixels. A Material whose `field` pin reaches a Shade node walks the chain upstream, writes one
// Material-domain Field program (one local per node, named after its place in the walk) and registers it with
// MaterialRegistry, so Render 3D's raster, the Metal engine and the tracers all run it through the row 1.3 hook.
//
// Coordinates are Infinite's (Y up). Every value is a vec3; scalars travel as vec3(v).

#include "core/INode.h"
#include "core/ImageCable.h"

#include <map>
#include <string>
#include <vector>

class ShadeNode;

// One composition pass: the program text so far, the local each visited node got, the bounds Generated uses.
struct ShadeBuild
{
   std::string body;
   std::map<const ShadeNode*, std::string> vars;
   float lo[3] = { 0.0f, 0.0f, 0.0f };
   float hi[3] = { 1.0f, 1.0f, 1.0f };
   int depth = 0;
   int counter = 0;
   std::string error;
   std::vector<const ShadeNode*> images; // row 2.4: Shade Image nodes in slot order (img(slot, uv)), at most 4
   std::vector<std::string> heights;     // row 2.6: what each Shade Bump adds to the `height` output

   // The local holding `c`'s upstream Shade node, emitting it first; `fallback` when nothing (or not a Shade node)
   // is wired.
   std::string Input(const ImageCable& c, const std::string& fallback);
   std::string Generated() const;
   std::string NewVar(const ShadeNode* n, const std::string& expr);
};

class ShadeNode : public INode
{
public:
   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   // A shade node draws nothing itself; cooking it cooks what is wired in (a Shade Image's picture, further up).
   void CookIfNeeded(int frameId) override
   {
      if (mShadeCookFrame == frameId) return;
      mShadeCookFrame = frameId;
      for (int i = 0; i < ShadeInputCount(); i++)
         if (ImageCable* c = ShadeInput(i))
            if (c->IsConnected()) c->Pull(frameId);
   }
   // Emits this node into b and returns the local that holds its value. Called once per node per pass.
   virtual std::string Emit(ShadeBuild& b) const = 0;
   // Is this node's value a colour (Material writes it to `base`) rather than a coordinate.
   virtual bool IsColour() const { return false; }
   // Image-cable inputs, in pin order (Wiring counts and resolves them through here).
   virtual int ShadeInputCount() const { return 0; }
   virtual ImageCable* ShadeInput(int) { return nullptr; }
   // Bypassed with one input: that input passes straight through (two or more inputs never bypass).
   INode* BypassSource() override
   {
      ImageCable* c = ShadeInputCount() == 1 ? ShadeInput(0) : nullptr;
      return c != nullptr ? c->GetSource() : nullptr;
   }
   // Shade Image only: the picture it reads and that picture's revision (0: nothing wired).
   virtual unsigned int ShadeImageTexture() const { return 0; }
   virtual unsigned long long ShadeImageRevision() const { return 0; }

private:
   int mShadeCookFrame = -1;
};

// Shade Coord: one texture coordinate (Blender's Texture Coordinate outputs, as one dropdown).
class ShadeCoordNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeCoordNode(); }
   enum Output { kUv = 0, kObject, kWorld, kGenerated, kNormal, kView, kReflection };
   static const std::vector<std::string>& OutputNames();
   int output = kUv;
   std::string Emit(ShadeBuild& b) const override;
   void VisitParams(ParamVisitor& v) override { v.Int("output", output); }
};

// Shade Mapping: translate, rotate (degrees, XYZ Euler), scale, as a point, a texture (the inverse), a vector (no
// translate) or a normal (inverse-transpose, unit length). Unwired: Generated.
class ShadeMappingNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeMappingNode(); }
   enum Kind { kPoint = 0, kTexture, kVector, kNormal };
   static const std::vector<std::string>& KindNames();
   int kind = kPoint;
   float translate[3] = { 0.0f, 0.0f, 0.0f };
   float rotate[3] = { 0.0f, 0.0f, 0.0f };
   float scale[3] = { 1.0f, 1.0f, 1.0f };
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("kind", kind);
      v.Float("translateX", translate[0]); v.Float("translateY", translate[1]); v.Float("translateZ", translate[2]);
      v.Float("rotateX", rotate[0]); v.Float("rotateY", rotate[1]); v.Float("rotateZ", rotate[2]);
      v.Float("scaleX", scale[0]); v.Float("scaleY", scale[1]); v.Float("scaleZ", scale[2]);
   }

private:
   ImageCable mVector;
};

// Shade Checker: two colours on a unit-cube checker of vector * scale. Unwired vector: Generated.
class ShadeCheckerNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeCheckerNode(); }
   float color1[3] = { 0.8f, 0.8f, 0.8f };
   float color2[3] = { 0.2f, 0.2f, 0.2f };
   float scale = 5.0f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   // Pins after the first override the knob of the same name while wired (appended, so old patches keep their slots).
   int ShadeInputCount() const override { return 4; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 4 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override
   {
      static const char* k[] = { "vector", "colour 1", "colour 2", "scale" };
      return slot >= 0 && slot < 4 ? k[slot] : nullptr;
   }
   void VisitParams(ParamVisitor& v) override
   {
      v.Color("color1", color1);
      v.Color("color2", color2);
      v.Float("scale", scale);
   }

private:
   ImageCable mIn[4];
};

// Shade Noise (row 2.4): fbm of the 2.1 gradient noise. detail = extra octaves, roughness = gain per octave,
// distortion = how far a second noise pushes the coordinate. Value: grey 0..1; Colour: three decorrelated values.
class ShadeNoiseNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeNoiseNode(); }
   enum Output { kValue = 0, kColour };
   static const std::vector<std::string>& OutputNames();
   int output = kValue;
   float scale = 5.0f;
   float detail = 2.0f;
   float roughness = 0.5f;
   float distortion = 0.0f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   // Pins after the first override the knob of the same name while wired (appended, so old patches keep their slots).
   int ShadeInputCount() const override { return 4; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 4 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override
   {
      static const char* k[] = { "vector", "scale", "roughness", "distortion" };
      return slot >= 0 && slot < 4 ? k[slot] : nullptr;
   }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("output", output);
      v.Float("scale", scale);
      v.Float("detail", detail);
      v.Float("roughness", roughness);
      v.Float("distortion", distortion);
   }

private:
   ImageCable mIn[4];
};

// Shade Voronoi (row 2.4): the 2.1 cell noise. Distance (F1), Second (F2), Edge (F2 - F1), Cell (grey per cell),
// Colour (a colour per cell).
class ShadeVoronoiNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeVoronoiNode(); }
   enum Output { kDistance = 0, kSecond, kEdge, kCell, kColour };
   static const std::vector<std::string>& OutputNames();
   int output = kDistance;
   float scale = 5.0f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   // Pins after the first override the knob of the same name while wired (appended, so old patches keep their slots).
   int ShadeInputCount() const override { return 2; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 2 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override
   {
      static const char* k[] = { "vector", "scale" };
      return slot >= 0 && slot < 2 ? k[slot] : nullptr;
   }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("output", output);
      v.Float("scale", scale);
   }

private:
   ImageCable mIn[2];
};

// Shade Gradient (row 2.4): a grey ramp of the coordinate. Linear to Radial are Blender's Gradient Texture types;
// Wave (bands along x) and Magic (interfering sines) stand in for its Wave and Magic textures.
class ShadeGradientNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeGradientNode(); }
   enum Kind { kLinear = 0, kQuadratic, kEasing, kDiagonal, kSpherical, kQuadraticSphere, kRadial, kWave, kMagic };
   static const std::vector<std::string>& KindNames();
   int kind = kLinear;
   float scale = 1.0f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("kind", kind);
      v.Float("scale", scale);
   }

private:
   ImageCable mVector;
};

// Shade Brick (row 2.4): bricks in the xy of vector * scale. width / height: one brick; offset: how far every
// second row shifts, in bricks; mortar: the gap from each brick edge; tint: how far each brick moves from colour 1
// to colour 2 by a per-brick random.
class ShadeBrickNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeBrickNode(); }
   float color1[3] = { 0.8f, 0.8f, 0.8f };
   float color2[3] = { 0.2f, 0.2f, 0.2f };
   float mortarColor[3] = { 0.0f, 0.0f, 0.0f };
   float scale = 5.0f;
   float width = 0.5f;
   float height = 0.25f;
   float offset = 0.5f;
   float mortar = 0.02f;
   float tint = 1.0f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   // Pins after the first override the knob of the same name while wired (appended, so old patches keep their slots).
   int ShadeInputCount() const override { return 5; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 5 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override
   {
      static const char* k[] = { "vector", "colour 1", "colour 2", "mortar", "scale" };
      return slot >= 0 && slot < 5 ? k[slot] : nullptr;
   }
   void VisitParams(ParamVisitor& v) override
   {
      v.Color("color1", color1);
      v.Color("color2", color2);
      v.Color("mortarColor", mortarColor);
      v.Float("scale", scale);
      v.Float("width", width);
      v.Float("height", height);
      v.Float("offset", offset);
      v.Float("mortar", mortar);
      v.Float("tint", tint);
   }

private:
   ImageCable mIn[5];
};

// Shade Image (row 2.4): any image cable as a texture, filtered with mips. Flat reads it at vector.xy (unwired: UV);
// Box projects it along the three axes and blends by the normal (triplanar). Colour decodes sRGB, Data reads raw.
// A material program reads at most four pictures.
class ShadeImageNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeImageNode(); }
   enum Projection { kFlat = 0, kBox };
   enum Space { kColour = 0, kData };
   static const std::vector<std::string>& ProjectionNames();
   static const std::vector<std::string>& SpaceNames();
   int projection = kFlat;
   int space = kColour;
   float blend = 0.2f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 2; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : i == 1 ? &mImage : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : slot == 1 ? "image" : nullptr; }
   unsigned int ShadeImageTexture() const override { return mImage.Texture(); }
   unsigned long long ShadeImageRevision() const override { return mImage.Revision(); }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("projection", projection);
      v.Int("space", space);
      v.Float("blend", blend);
   }

private:
   ImageCable mVector;
   ImageCable mImage;
};

// ---- Value nodes (row 2.5) ----
// A scalar pin reads the x of what is wired in; an unwired pin uses the node's own number.

// Shade Math: Blender's scalar set. a, b, c are the three operands (c: Multiply Add's addend, Compare's epsilon,
// Smooth Min / Max's distance, Wrap's min). Division by zero, roots and logs of negatives give 0.
class ShadeMathNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeMathNode(); }
   enum Op
   {
      kAdd = 0, kSubtract, kMultiply, kDivide, kMultiplyAdd, kPower, kLogarithm, kSqrt, kInvSqrt, kAbsolute, kExponent,
      kMinimum, kMaximum, kLessThan, kGreaterThan, kSign, kCompare, kSmoothMin, kSmoothMax,
      kRound, kFloor, kCeil, kTruncate, kFraction, kTruncModulo, kFloorModulo, kWrap, kSnap, kPingPong,
      kSine, kCosine, kTangent, kArcsine, kArccosine, kArctangent, kArctan2, kSinh, kCosh, kTanh,
      kToRadians, kToDegrees, kOpCount
   };
   static const std::vector<std::string>& OpNames();
   // How many operands `op` reads (1 to 3): the body shows that many.
   static int Operands(int op);
   int op = kAdd;
   float value[3] = { 0.5f, 0.5f, 0.0f };
   bool clampResult = false;
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 3; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 3 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "a" : slot == 1 ? "b" : slot == 2 ? "c" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op);
      v.Float("a", value[0]); v.Float("b", value[1]); v.Float("c", value[2]);
      v.Bool("clamp", clampResult);
   }

private:
   ImageCable mIn[3];
};

// Shade Vector Math: Blender's vector set, per component where that makes sense; Dot, Distance and Length give a
// grey value. c is Multiply Add's addend, Faceforward's reference, Wrap's min; `scale` is Scale's factor and
// Refract's index ratio.
class ShadeVectorMathNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeVectorMathNode(); }
   enum Op
   {
      kAdd = 0, kSubtract, kMultiply, kDivide, kMultiplyAdd, kCross, kProject, kReflect, kRefract, kFaceforward,
      kDot, kDistance, kLength, kScale, kNormalize, kAbsolute, kMinimum, kMaximum, kFloor, kCeil, kFraction,
      kModulo, kWrap, kSnap, kSine, kCosine, kTangent, kOpCount
   };
   static const std::vector<std::string>& OpNames();
   static int Operands(int op); // vector operands read (1 to 3)
   static bool UsesScale(int op);
   int op = kAdd;
   float a[3] = { 0.0f, 0.0f, 0.0f };
   float b[3] = { 0.0f, 0.0f, 0.0f };
   float c[3] = { 0.0f, 0.0f, 0.0f };
   float scale = 1.0f;
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 3; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 3 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "a" : slot == 1 ? "b" : slot == 2 ? "c" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("op", op);
      v.Float("aX", a[0]); v.Float("aY", a[1]); v.Float("aZ", a[2]);
      v.Float("bX", b[0]); v.Float("bY", b[1]); v.Float("bZ", b[2]);
      v.Float("cX", c[0]); v.Float("cY", c[1]); v.Float("cZ", c[2]);
      v.Float("scale", scale);
   }

private:
   ImageCable mIn[3];
};

// Shade Mix: a to b by factor. Float and Vector blend straight; Colour runs b through one of the blend modes of
// core/BlendModes.h (the 30 colour ones, same formulas) first: mix(a, blend(a, b), factor).
class ShadeMixNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeMixNode(); }
   enum DataType { kFloat = 0, kVector, kColour };
   static const std::vector<std::string>& DataTypeNames();
   static const std::vector<std::string>& ModeNames(); // the first 30 of BlendModes::Names()
   int dataType = kColour;
   int mode = 0;
   float factor = 0.5f;
   float a[3] = { 0.5f, 0.5f, 0.5f }; // Float reads a[0] / b[0]; Colour treats them as sRGB swatches
   float b[3] = { 0.5f, 0.5f, 0.5f };
   bool clampFactor = true;
   bool clampResult = false;
   bool IsColour() const override { return dataType == kColour; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 3; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 3 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "factor" : slot == 1 ? "a" : slot == 2 ? "b" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("dataType", dataType);
      v.Int("mode", mode);
      v.Float("factor", factor);
      v.Color("a", a);
      v.Color("b", b);
      v.Bool("clampFactor", clampFactor);
      v.Bool("clampResult", clampResult);
   }

private:
   ImageCable mIn[3];
};

// Shade Ramp: a colour ramp of 2 to 8 stops read at the factor. Below the first stop and above the last the end
// colours hold. B-Spline treats the stops as control points of a uniform cubic B-spline (ends repeated).
class ShadeRampNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeRampNode(); }
   enum Interp { kLinear = 0, kEase, kConstant, kBSpline };
   static const std::vector<std::string>& InterpNames();
   static constexpr int kMaxStops = 8;
   int interp = kLinear;
   int count = 2;
   float factor = 0.5f;
   float pos[kMaxStops] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
   float color[kMaxStops][3] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
                                 { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mFactor : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "factor" : nullptr; }
   void VisitParams(ParamVisitor& v) override;

private:
   ImageCable mFactor;
};

// Shade Map Range: value from [fromMin, fromMax] to [toMin, toMax]. Only the value is a pin.
class ShadeMapRangeNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeMapRangeNode(); }
   enum Interp { kLinear = 0, kStepped, kSmoothstep, kSmootherstep };
   static const std::vector<std::string>& InterpNames();
   int interp = kLinear;
   float value = 0.5f;
   float fromMin = 0.0f, fromMax = 1.0f, toMin = 0.0f, toMax = 1.0f;
   float steps = 4.0f;
   bool clampResult = true;
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mValue : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "value" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("interp", interp);
      v.Float("value", value);
      v.Float("fromMin", fromMin); v.Float("fromMax", fromMax);
      v.Float("toMin", toMin); v.Float("toMax", toMax);
      v.Float("steps", steps);
      v.Bool("clamp", clampResult);
   }

private:
   ImageCable mValue;
};

// Shade Separate: one channel of a vector or colour as a grey value (RGB / XYZ, or hue, saturation, value).
class ShadeSeparateNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeSeparateNode(); }
   static const std::vector<std::string>& ModeNames();    // "RGB / XYZ", "HSV"
   static const std::vector<std::string>& ChannelNames(int mode);
   int mode = 0;
   int channel = 0;
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mIn : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
   void VisitParams(ParamVisitor& v) override { v.Int("mode", mode); v.Int("channel", channel); }

private:
   ImageCable mIn;
};

// Shade Combine: three values into one vector or colour (RGB / XYZ, or hue, saturation, value).
class ShadeCombineNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeCombineNode(); }
   int mode = 0;
   float value[3] = { 0.0f, 0.0f, 0.0f };
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 3; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 3 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "x" : slot == 1 ? "y" : slot == 2 ? "z" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("mode", mode);
      v.Float("x", value[0]); v.Float("y", value[1]); v.Float("z", value[2]);
   }

private:
   ImageCable mIn[3];
};

// Shade Curves: one curve through 2 to 8 points, applied to each channel, blended in by factor. Between points a
// cubic Hermite with Catmull-Rom slopes; outside them the end slope carries on, so the default (0,0)-(1,1) is identity.
class ShadeCurvesNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeCurvesNode(); }
   static constexpr int kMaxPoints = 8;
   int count = 2;
   float factor = 1.0f;
   float px[kMaxPoints] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
   float py[kMaxPoints] = { 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mIn : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "colour" : nullptr; }
   void VisitParams(ParamVisitor& v) override;

private:
   ImageCable mIn;
};

// Shade Bump: a height bends the normal. The height goes to the Material domain's `height` output (strength x distance,
// in scene units); the renderer takes its gradient (screen derivatives in the raster and live engines, two more taps
// in the tracer). Its value is the tangent-space normal wired into `normal` (flat when nothing is), so it plugs into
// Material's normal pin.
class ShadeBumpNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeBumpNode(); }
   float strength = 1.0f;
   float distance = 0.1f;
   bool invert = false;
   std::string Emit(ShadeBuild& b) const override;
   // Pins after the first override the knob of the same name while wired (appended, so old patches keep their slots).
   int ShadeInputCount() const override { return 4; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 4 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override
   {
      static const char* k[] = { "height", "normal", "strength", "distance" };
      return slot >= 0 && slot < 4 ? k[slot] : nullptr;
   }
   void VisitParams(ParamVisitor& v) override
   {
      v.Float("strength", strength);
      v.Float("distance", distance);
      v.Bool("invert", invert);
   }

private:
   ImageCable mIn[4];
};

// Shade Normal Map: a tangent-space normal map colour (wire a Shade Image set to Data) as a tangent-space normal for
// Material's normal pin. Strength leans it from flat (0) to the map (1) and past it.
class ShadeNormalMapNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeNormalMapNode(); }
   float strength = 1.0f;
   std::string Emit(ShadeBuild& b) const override;
   // Pins after the first override the knob of the same name while wired (appended, so old patches keep their slots).
   int ShadeInputCount() const override { return 2; }
   ImageCable* ShadeInput(int i) override { return i >= 0 && i < 2 ? &mIn[i] : nullptr; }
   const char* InputLabel(int slot) const override
   {
      static const char* k[] = { "colour", "strength" };
      return slot >= 0 && slot < 2 ? k[slot] : nullptr;
   }
   void VisitParams(ParamVisitor& v) override { v.Float("strength", strength); }

private:
   ImageCable mIn[2];
};

// Shade Fresnel: how much a dielectric of this IOR reflects at the view angle (0 facing, 1 grazing).
class ShadeFresnelNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeFresnelNode(); }
   float ior = 1.45f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mIor : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "ior" : nullptr; }
   void VisitParams(ParamVisitor& v) override { v.Float("ior", ior); }

private:
   ImageCable mIor;
};

// Shade Layer Weight: a blend weight by view angle, as a Fresnel curve or a plain facing falloff.
class ShadeLayerWeightNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeLayerWeightNode(); }
   enum Output { kFresnel = 0, kFacing };
   static const std::vector<std::string>& OutputNames();
   int output = kFresnel;
   float blend = 0.5f;
   bool IsColour() const override { return true; }
   std::string Emit(ShadeBuild& b) const override;
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mBlend : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "blend" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("output", output);
      v.Float("blend", blend);
   }

private:
   ImageCable mBlend;
};

// Shade Geometry: what the surface point knows about itself. Curvature and cavity are 0 until row 4.4, the per-object
// random is 0 until row 7.2.
class ShadeGeometryNode : public ShadeNode
{
public:
   static INode* Create() { return new ShadeGeometryNode(); }
   enum Output { kPosition = 0, kNormal, kTangent, kIncoming, kBackfacing, kCurvature, kCavity, kRandom };
   static const std::vector<std::string>& OutputNames();
   int output = kPosition;
   std::string Emit(ShadeBuild& b) const override;
   void VisitParams(ParamVisitor& v) override { v.Int("output", output); }
};

// One Material output and the Shade node that drives it. Channels: base, roughness, metallic, normal, emission, opacity.
struct ShadeOutput
{
   const char* channel;
   const ShadeNode* node;
};

// The Material-domain program for a chain ending at `out` (empty with `err` set on failure). lo/hi: the object-space
// bounds Generated normalises by. images: the Shade Image nodes the program reads, in slot order.
std::string ComposeShadeProgram(const ShadeNode* out, const float lo[3], const float hi[3], std::string& err,
                                std::vector<const ShadeNode*>* images = nullptr);
// The same for several outputs at once (row 2.6): one program writes every channel given; Shade Bump nodes on the
// way add up into `height`.
std::string ComposeShadeProgram(const std::vector<ShadeOutput>& outs, const float lo[3], const float hi[3],
                                std::string& err, std::vector<const ShadeNode*>* images = nullptr);
