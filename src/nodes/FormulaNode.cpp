#include "FormulaNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>

#include "Transport.h"

namespace
{
   // The user writes the body of shape(); everything above is provided for them.
   const char* kPreamble =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform float uTime;\n"
      "uniform float uA;\n"
      "uniform float uB;\n"
      "uniform float uC;\n"
      "uniform float uD;\n"
      "\n"
      "#define PI 3.14159265359\n"
      "#define TAU 6.28318530718\n"
      "float rand(vec2 co) { return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453); }\n"
      "mat2 rot(float a) { float s = sin(a), c = cos(a); return mat2(c, -s, s, c); }\n"
      "float sdCircle(vec2 p, float r) { return length(p) - r; }\n"
      "float sdBox(vec2 p, vec2 b) { vec2 d = abs(p) - b; return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0); }\n"
      "\n"
      "// uv is 0..1, p is centered -0.5..0.5, t is seconds, knobs uA..uD are 0..1.\n"
      "// Return the final RGBA for this pixel.\n"
      "vec4 shape(vec2 uv, vec2 p, float t) {\n";

   const char* kEpilogue =
      "\n}\n"
      "void main() { fragColor = shape(vUv, vUv - vec2(0.5), uTime); }\n";

   const char* kDefaultFormula =
      "   float d = sdCircle(p, 0.2 + uA * 0.2);\n"
      "   d += sin(atan(p.y, p.x) * 6.0 + t) * uB * 0.08;\n"
      "   float mask = smoothstep(0.005, -0.005, d);\n"
      "   vec3 col = mix(vec3(0.05), vec3(uC, 0.6, 1.0 - uC), mask);\n"
      "   return vec4(col, 1.0);";
}

FormulaNode::FormulaNode()
{
   formula = kDefaultFormula;
}

FormulaNode::~FormulaNode()
{
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   GLUtil::DestroyFbo(mOut);
}

bool FormulaNode::Apply()
{
   std::string src = std::string(kPreamble) + formula + kEpilogue;

   std::string error;
   unsigned int program = GLUtil::CompileProgram(src.c_str(), &error);
   if (program == 0)
   {
      mLastError = error;
      return false;
   }

   if (mProgram != 0)
      glDeleteProgram(mProgram);
   mProgram = program;
   mLastError.clear();
   return true;
}

void FormulaNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (mProgram == 0 && mLastError.empty())
      Apply();
   if (mProgram == 0)
      return;

   if (animate)
      mClock = (float)Transport::Instance().Seconds();

   const int w = std::max(4, (int)width);
   const int h = std::max(4, (int)height);
   if (!GLUtil::EnsureFbo(mOut, w, h))
      return;

   GLUtil::RunShaderPass(mOut, mProgram, [this]()
   {
      glUniform1f(glGetUniformLocation(mProgram, "uTime"), mClock);
      glUniform1f(glGetUniformLocation(mProgram, "uA"), knobA);
      glUniform1f(glGetUniformLocation(mProgram, "uB"), knobB);
      glUniform1f(glGetUniformLocation(mProgram, "uC"), knobC);
      glUniform1f(glGetUniformLocation(mProgram, "uD"), knobD);
   });
}
