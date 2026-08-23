#include "ShapeNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>

namespace
{
   // Append only - `shapeType` is stored in patch files as an index, so
   // reordering this list would silently change every saved patch.
   const std::vector<std::string> kShapeNames = {
      "Circle", "Ellipse", "Rectangle", "Rounded Rect", "Triangle",
      "Polygon", "Star", "Ring", "Cross", "Line",
      "Hexagon", "Heart", "Arrow", "Crescent", "Gear",
      "Superellipse", "Pie", "Teardrop", "Chevron", "Blob"
   };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform int uShape;\n"
      "uniform vec2 uSize;\n"
      "uniform float uCornerRadius;\n"
      "uniform int uSides;\n"
      "uniform float uInnerRatio;\n"
      "uniform float uRotation;\n"
      "uniform vec2 uPos;\n"
      "uniform vec3 uFillColor;\n"
      "uniform float uFillOpacity;\n"
      "uniform float uStrokeWidth;\n"
      "uniform vec3 uStrokeColor;\n"
      "uniform float uFeather;\n"
      "uniform vec3 uBgColor;\n"
      "uniform float uBgOpacity;\n"
      "uniform float uAspect;\n"
      "\n"
      "float sdCircle(vec2 p, float r) { return length(p) - r; }\n"
      "float sdBox(vec2 p, vec2 b) {\n"
      "   vec2 d = abs(p) - b;\n"
      "   return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);\n"
      "}\n"
      "float sdRoundBox(vec2 p, vec2 b, float r) { return sdBox(p, b - vec2(r)) - r; }\n"
      "float sdNgon(vec2 p, float r, int n) {\n"
      "   float a = atan(p.y, p.x);\n"
      "   float seg = 6.2831853 / float(max(n, 3));\n"
      "   float d = cos(floor(0.5 + a/seg) * seg - a) * length(p);\n"
      "   return d - r;\n"
      "}\n"
      "float sdStar(vec2 p, float r, int n, float inner) {\n"
      "   float a = atan(p.y, p.x);\n"
      "   float seg = 6.2831853 / float(max(n, 3));\n"
      "   float wedge = mod(a, seg) / seg;\n"
      "   float t = abs(wedge - 0.5) * 2.0;\n"
      "   float radius = mix(r * inner, r, t);\n"
      "   return length(p) - radius;\n"
      "}\n"
      "float sdCross(vec2 p, vec2 b) {\n"
      "   return min(sdBox(p, b), sdBox(p, b.yx));\n"
      "}\n"
      "float sdSegment(vec2 p, vec2 a, vec2 b, float th) {\n"
      "   vec2 pa = p - a, ba = b - a;\n"
      "   float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);\n"
      "   return length(pa - ba*h) - th;\n"
      "}\n"
      "float dot2(vec2 v) { return dot(v, v); }\n"
      // Exact distance to the classic cardioid-ish heart, so stroke and feather
      // behave the same here as on a circle. Spans roughly x in [-1,1],
      // y in [0,1] with the tip at the origin, hence the caller's y offset.
      "float sdHeart(vec2 p) {\n"
      "   p.x = abs(p.x);\n"
      "   if (p.y + p.x > 1.0)\n"
      "      return sqrt(dot2(p - vec2(0.25, 0.75))) - sqrt(2.0) / 4.0;\n"
      "   return sqrt(min(dot2(p - vec2(0.0, 1.0)),\n"
      "                   dot2(p - 0.5 * max(p.x + p.y, 0.0)))) * sign(p.x - p.y);\n"
      "}\n"
      // Isosceles triangle as the intersection of three half-planes: apex at
      // (apexX, 0), base at x = baseX, half-height h. Exact inside, a slight
      // under-estimate around the outer corners - invisible at mask scale.
      "float sdTri(vec2 p, float apexX, float baseX, float h) {\n"
      "   vec2 n = normalize(vec2(h, apexX - baseX));\n"
      "   return max(baseX - p.x, dot(vec2(p.x, abs(p.y)) - vec2(apexX, 0.0), n));\n"
      "}\n"
      "float sdPie(vec2 p, float r, float frac) {\n"
      "   float ang = 3.14159265 * clamp(frac, 0.001, 0.999);\n"
      "   vec2 c = vec2(sin(ang), cos(ang));\n"
      "   p.x = abs(p.x);\n"
      "   float l = length(p) - r;\n"
      "   float m = length(p - c * clamp(dot(p, c), 0.0, r));\n"
      "   return max(l, m * sign(c.y * p.x - c.x * p.y));\n"
      "}\n"
      "float sdGear(vec2 p, float r, int teeth, float hub) {\n"
      "   float a = atan(p.y, p.x);\n"
      "   float len = length(p);\n"
      "   float tooth = cos(a * float(max(teeth, 3)));\n"
      "   float outer = r * (0.80 + 0.20 * smoothstep(-0.35, 0.35, tooth));\n"
      "   return max(len - outer, hub * r - len);\n"
      "}\n"
      "float sdSuperellipse(vec2 p, float r, float n) {\n"
      "   vec2 q = abs(p) / max(r, 1e-4);\n"
      "   float f = pow(pow(q.x, n) + pow(q.y, n), 1.0 / n);\n"
      "   return (f - 1.0) * r;\n"
      "}\n"
      "float sdBlob(vec2 p, float r, int lobes) {\n"
      "   float a = atan(p.y, p.x);\n"
      "   float k = float(max(lobes, 2));\n"
      "   return length(p) - r * (1.0 + 0.24 * sin(k * a) + 0.11 * sin(k * 2.0 * a + 1.7));\n"
      "}\n"
      "\n"
      "void main() {\n"
      "   vec2 p = vUv - uPos;\n"
      "   p.x *= uAspect;\n"
      "   float s = sin(-uRotation), c = cos(-uRotation);\n"
      "   p = vec2(c*p.x - s*p.y, s*p.x + c*p.y);\n"
      "\n"
      "   float baseRadius = max(uSize.y, 0.001);\n"
      "   float shapeAspect = max(uSize.x / baseRadius, 0.0001);\n"
      "   vec2 sp = vec2(p.x / shapeAspect, p.y);\n"
      "\n"
      "   float d;\n"
      "   if (uShape == 0) d = sdCircle(sp, baseRadius) * min(shapeAspect, 1.0);\n"
      "   else if (uShape == 1) d = sdCircle(sp * vec2(1.0, 1.6), baseRadius);\n"
      "   else if (uShape == 2) d = sdBox(p, max(uSize, vec2(0.001)));\n"
      "   else if (uShape == 3) d = sdRoundBox(p, max(uSize, vec2(0.001)), min(uCornerRadius, min(uSize.x, uSize.y)));\n"
      "   else if (uShape == 4) d = sdNgon(sp, baseRadius, 3);\n"
      "   else if (uShape == 5) d = sdNgon(sp, baseRadius, uSides);\n"
      "   else if (uShape == 6) d = sdStar(sp, baseRadius, uSides, uInnerRatio);\n"
      "   else if (uShape == 7) d = abs(sdCircle(sp, baseRadius)) - max(uCornerRadius, 0.001);\n"
      "   else if (uShape == 8) d = sdCross(p, max(uSize, vec2(0.001)));\n"
      "   else if (uShape == 9) d = sdSegment(p, vec2(-uSize.x, 0.0), vec2(uSize.x, 0.0), max(uCornerRadius, 0.002));\n"
      "   else if (uShape == 10) d = sdNgon(sp, baseRadius, 6);\n"
      "   else if (uShape == 11) d = sdHeart(vec2(sp.x, sp.y + baseRadius * 0.5) / baseRadius) * baseRadius;\n"
      "   else if (uShape == 12) {\n"
      "      float shaft = sdBox(p - vec2(-uSize.x * 0.30, 0.0), vec2(uSize.x * 0.50, uSize.y * 0.22));\n"
      "      float head = sdTri(p, uSize.x, uSize.x * 0.20, uSize.y * 0.55);\n"
      "      d = min(shaft, head);\n"
      "   }\n"
      "   else if (uShape == 13) d = max(sdCircle(sp, baseRadius),\n"
      "                                  -sdCircle(sp - vec2(baseRadius * 0.45, 0.0), baseRadius * 0.88));\n"
      "   else if (uShape == 14) d = sdGear(sp, baseRadius, uSides, uInnerRatio * 0.7);\n"
      "   else if (uShape == 15) d = sdSuperellipse(sp, baseRadius, mix(2.0, 10.0, uInnerRatio));\n"
      "   else if (uShape == 16) d = sdPie(sp, baseRadius, uInnerRatio);\n"
      "   else if (uShape == 17) {\n"
      "      float ball = sdCircle(sp - vec2(0.0, -baseRadius * 0.30), baseRadius * 0.62);\n"
      "      float tip = sdTri(vec2(sp.y, sp.x), baseRadius, -baseRadius * 0.30, baseRadius * 0.62);\n"
      "      d = min(ball, tip);\n"
      "   }\n"
      "   else if (uShape == 18) d = sdSegment(vec2(p.x, abs(p.y)),\n"
      "                                        vec2(-uSize.x * 0.6, uSize.y * 0.8), vec2(uSize.x * 0.6, 0.0),\n"
      "                                        max(uCornerRadius, 0.02));\n"
      "   else d = sdBlob(sp, baseRadius, uSides);\n"
      "\n"
      "   float aa = max(uFeather, 1e-4);\n"
      "   float fillMask = smoothstep(aa, -aa, d) * uFillOpacity;\n"
      "   float strokeMask = 0.0;\n"
      "   if (uStrokeWidth > 0.0)\n"
      "      strokeMask = smoothstep(aa, -aa, abs(d) - uStrokeWidth);\n"
      "\n"
      "   vec4 col = vec4(uBgColor, uBgOpacity);\n"
      "   col.rgb = mix(col.rgb, uFillColor, fillMask);\n"
      "   col.a = max(col.a, fillMask);\n"
      "   col.rgb = mix(col.rgb, uStrokeColor, strokeMask);\n"
      "   col.a = max(col.a, strokeMask);\n"
      "   fragColor = col;\n"
      "}\n";
}

const std::vector<std::string>& ShapeNode::ShapeNames()
{
   return kShapeNames;
}

ShapeNode::~ShapeNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
}

bool ShapeNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;
   mProgram = GLUtil::CompileProgram(kFragSrc);
   return mProgram != 0;
}

void ShapeNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShader())
      return;

   const int w = std::max(4, (int)width);
   const int h = std::max(4, (int)height);
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this]()
   {
      glUniform1i(glGetUniformLocation(mProgram, "uShape"), shapeType);
      glUniform2f(glGetUniformLocation(mProgram, "uSize"), sizeX, sizeY);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), (float)mOut.w / (float)mOut.h);
      glUniform1f(glGetUniformLocation(mProgram, "uCornerRadius"), cornerRadius);
      glUniform1i(glGetUniformLocation(mProgram, "uSides"), sides);
      glUniform1f(glGetUniformLocation(mProgram, "uInnerRatio"), innerRatio);
      glUniform1f(glGetUniformLocation(mProgram, "uRotation"), rotation * (float)M_PI / 180.0f);
      glUniform2f(glGetUniformLocation(mProgram, "uPos"), posX, posY);
      glUniform3f(glGetUniformLocation(mProgram, "uFillColor"), fillColor[0], fillColor[1], fillColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uFillOpacity"), fillOpacity);
      glUniform1f(glGetUniformLocation(mProgram, "uStrokeWidth"), strokeWidth);
      glUniform3f(glGetUniformLocation(mProgram, "uStrokeColor"), strokeColor[0], strokeColor[1], strokeColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uFeather"), feather);
      glUniform3f(glGetUniformLocation(mProgram, "uBgColor"), bgColor[0], bgColor[1], bgColor[2]);
      glUniform1f(glGetUniformLocation(mProgram, "uBgOpacity"), bgOpacity);
   });
}
