#include "ShapeNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>

namespace
{
   const std::vector<std::string> kShapeNames = {
      "Circle", "Ellipse", "Rectangle", "Rounded Rect", "Triangle",
      "Polygon", "Star", "Ring", "Cross", "Line"
   };

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform int uShape;\n"
      "uniform float uSize;\n"
      "uniform float uAspect;\n"
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
      "\n"
      "void main() {\n"
      "   vec2 p = vUv - uPos;\n"
      "   float s = sin(-uRotation), c = cos(-uRotation);\n"
      "   p = vec2(c*p.x - s*p.y, s*p.x + c*p.y);\n"
      "   p.x /= max(uAspect, 0.0001);\n"
      "\n"
      "   float d;\n"
      "   if (uShape == 0) d = sdCircle(p, uSize);\n"
      "   else if (uShape == 1) d = sdCircle(p * vec2(1.0, 1.6), uSize);\n"
      "   else if (uShape == 2) d = sdBox(p, vec2(uSize));\n"
      "   else if (uShape == 3) d = sdRoundBox(p, vec2(uSize), min(uCornerRadius, uSize));\n"
      "   else if (uShape == 4) d = sdNgon(p, uSize, 3);\n"
      "   else if (uShape == 5) d = sdNgon(p, uSize, uSides);\n"
      "   else if (uShape == 6) d = sdStar(p, uSize, uSides, uInnerRatio);\n"
      "   else if (uShape == 7) d = abs(sdCircle(p, uSize)) - max(uCornerRadius, 0.001);\n"
      "   else if (uShape == 8) d = sdCross(p, vec2(uSize, uSize * 0.28));\n"
      "   else d = sdSegment(p, vec2(-uSize, 0.0), vec2(uSize, 0.0), max(uCornerRadius, 0.002));\n"
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
      glUniform1f(glGetUniformLocation(mProgram, "uSize"), size);
      glUniform1f(glGetUniformLocation(mProgram, "uAspect"), aspect * (float)mOut.w / (float)mOut.h);
      glUniform1f(glGetUniformLocation(mProgram, "uCornerRadius"), cornerRadius);
      glUniform1i(glGetUniformLocation(mProgram, "uSides"), sides);
      glUniform1f(glGetUniformLocation(mProgram, "uInnerRatio"), innerRatio);
      glUniform1f(glGetUniformLocation(mProgram, "uRotation"), rotation);
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
