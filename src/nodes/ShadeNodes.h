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
   void CookIfNeeded(int) override {}
   // Emits this node into b and returns the local that holds its value. Called once per node per pass.
   virtual std::string Emit(ShadeBuild& b) const = 0;
   // Is this node's value a colour (Material writes it to `base`) rather than a coordinate.
   virtual bool IsColour() const { return false; }
   // Image-cable inputs, in pin order (Wiring counts and resolves them through here).
   virtual int ShadeInputCount() const { return 0; }
   virtual ImageCable* ShadeInput(int) { return nullptr; }
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
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Color("color1", color1);
      v.Color("color2", color2);
      v.Float("scale", scale);
   }

private:
   ImageCable mVector;
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
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("output", output);
      v.Float("scale", scale);
      v.Float("detail", detail);
      v.Float("roughness", roughness);
      v.Float("distortion", distortion);
   }

private:
   ImageCable mVector;
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
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
   void VisitParams(ParamVisitor& v) override
   {
      v.Int("output", output);
      v.Float("scale", scale);
   }

private:
   ImageCable mVector;
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
   int ShadeInputCount() const override { return 1; }
   ImageCable* ShadeInput(int i) override { return i == 0 ? &mVector : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "vector" : nullptr; }
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
   ImageCable mVector;
};

// The Material-domain program for a chain ending at `out` (empty with `err` set on failure). lo/hi: the object-space
// bounds Generated normalises by.
std::string ComposeShadeProgram(const ShadeNode* out, const float lo[3], const float hi[3], std::string& err);
