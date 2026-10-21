#include "LSystemNode.h"

#include <algorithm>
#include <cmath>
#include <map>

constexpr int LSystemNode::kMaxIterations;
constexpr size_t LSystemNode::kMaxSymbols;
constexpr size_t LSystemNode::kMaxStrands;

namespace
{
   struct Turtle
   {
      float px = 0, py = 0, pz = 0;
      float h[3] = { 0, 1, 0 };    // heading
      float l[3] = { -1, 0, 0 };   // left
      float u[3] = { 0, 0, 1 };    // up
      int depth = 0;
   };

   // Rotates vectors a and b in their shared plane by `ang`.
   void Spin(float* a, float* b, float ang)
   {
      const float c = std::cos(ang), s = std::sin(ang);
      for (int i = 0; i < 3; i++)
      {
         const float na = a[i] * c + b[i] * s;
         const float nb = -a[i] * s + b[i] * c;
         a[i] = na;
         b[i] = nb;
      }
   }

   void Renormalize(Turtle& t)
   {
      float* v[3] = { t.h, t.l, t.u };
      for (float* x : v)
      {
         const float len = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
         if (len > 1e-6f)
            for (int i = 0; i < 3; i++)
               x[i] /= len;
      }
   }

   // "X=FF;Y=F[+X]" -> predecessor char -> replacement. ';' or newline separate
   // rules; a rule without '=' or with an empty predecessor is ignored, and the
   // first rule for a letter wins.
   std::map<char, std::string> ParseRules(const std::string& text)
   {
      std::map<char, std::string> out;
      size_t start = 0;
      while (start <= text.size())
      {
         size_t end = text.find_first_of(";\n", start);
         if (end == std::string::npos)
            end = text.size();
         std::string rule = text.substr(start, end - start);
         start = end + 1;
         rule.erase(std::remove_if(rule.begin(), rule.end(),
                                   [](char c) { return c == ' ' || c == '\t' || c == '\r'; }),
                    rule.end());
         const size_t eq = rule.find('=');
         if (eq == std::string::npos || eq == 0)
            continue;
         const char pred = rule[0];
         if (eq != 1)
            continue;
         if (out.find(pred) == out.end())
            out[pred] = rule.substr(eq + 1);
      }
      return out;
   }
}

void LSystemNode::RebuildIfNeeded()
{
   if (mHasBuilt && mBuiltAxiom == axiom && mBuiltRules == rules &&
       mBuiltIterations == iterations && mBuiltSides == sides && mBuiltAngle == angle &&
       mBuiltLength == length && mBuiltLengthScale == lengthScale && mBuiltRadius == radius &&
       mBuiltRadiusScale == radiusScale && mBuiltTaper == taper)
      return;

   mWarning.clear();
   mMesh = Mesh();
   mLine = Polyline();
   mSegments = 0;
   mStrands = 0;

   // --- rewrite ---------------------------------------------------------
   const int gens = std::max(0, std::min(iterations, kMaxIterations));
   const std::map<char, std::string> table = ParseRules(rules);
   std::string cur = axiom;
   if (cur.size() > kMaxSymbols)
      cur.resize(kMaxSymbols);
   int done = 0;
   for (int g = 0; g < gens; g++)
   {
      std::string next;
      next.reserve(cur.size() * 2);
      bool over = false;
      for (char c : cur)
      {
         auto it = table.find(c);
         if (it == table.end())
            next.push_back(c);
         else
            next += it->second;
         if (next.size() > kMaxSymbols)
         {
            over = true;
            break;
         }
      }
      if (over)
      {
         mWarning = "Grammar grows past " + std::to_string(kMaxSymbols) +
                    " symbols; stopped at generation " + std::to_string(done) + ".";
         break;
      }
      cur.swap(next);
      done++;
   }

   // --- walk ------------------------------------------------------------
   const float ang = angle * 3.14159265f / 180.0f;
   const int sideCount = std::max(3, std::min(sides, 24));
   const float lenScale = std::max(0.05f, std::min(lengthScale, 2.0f));
   const float radScale = std::max(0.05f, std::min(radiusScale, 2.0f));

   Turtle t;
   std::vector<Turtle> stack;
   std::vector<float> strand;      // current branch, xyz
   std::vector<float> strandRadius;
   float curRadius = std::max(0.0005f, radius);
   std::vector<float> radiusStack;
   bool strandsCapped = false;

   auto flush = [&]()
   {
      if (strand.size() >= 6)
      {
         if (mStrands >= kMaxStrands)
         {
            strandsCapped = true;
         }
         else
         {
            Polyline p;
            p.points = strand;
            Mesh tube = MeshOps::TubeAlong(p, strandRadius.empty() ? curRadius : strandRadius[0],
                                           sideCount, taper);
            MeshOps::AppendMesh(mMesh, tube);
            mStrands++;
         }
      }
      strand.clear();
      strandRadius.clear();
   };
   auto startStrand = [&]()
   {
      strand.assign({ t.px, t.py, t.pz });
      strandRadius.assign(1, curRadius);
   };
   auto addCurve = [&](float x, float y, float z)
   {
      mLine.points.push_back(x); mLine.points.push_back(y); mLine.points.push_back(z);
   };

   startStrand();
   addCurve(t.px, t.py, t.pz);
   float step = std::max(0.0001f, length);
   std::vector<float> stepStack;

   for (char c : cur)
   {
      switch (c)
      {
         case 'F':
         case 'G':
         case 'f':
         {
            t.px += t.h[0] * step; t.py += t.h[1] * step; t.pz += t.h[2] * step;
            if (c == 'f')
            {
               flush();
               startStrand();
            }
            else
            {
               strand.push_back(t.px); strand.push_back(t.py); strand.push_back(t.pz);
               mSegments++;
            }
            addCurve(t.px, t.py, t.pz);
            break;
         }
         case '+': Spin(t.h, t.l, ang); Renormalize(t); break;
         case '-': Spin(t.h, t.l, -ang); Renormalize(t); break;
         case '&': Spin(t.h, t.u, ang); Renormalize(t); break;
         case '^': Spin(t.h, t.u, -ang); Renormalize(t); break;
         case '\\': Spin(t.l, t.u, ang); Renormalize(t); break;
         case '/': Spin(t.l, t.u, -ang); Renormalize(t); break;
         case '|': Spin(t.h, t.l, 3.14159265f); Renormalize(t); break;
         case '[':
            if (stack.size() < 256)
            {
               flush();
               stack.push_back(t);
               radiusStack.push_back(curRadius);
               stepStack.push_back(step);
               curRadius *= radScale;
               step *= lenScale;
               startStrand();
            }
            break;
         case ']':
            if (!stack.empty())
            {
               flush();
               t = stack.back();
               curRadius = radiusStack.back();
               step = stepStack.back();
               stack.pop_back();
               radiusStack.pop_back();
               stepStack.pop_back();
               startStrand();
               addCurve(t.px, t.py, t.pz);
            }
            break;
         default:
            break;
      }
   }
   flush();

   if (strandsCapped)
      mWarning = "More than " + std::to_string(kMaxStrands) +
                 " branches; the rest are in the curve but not drawn as tubes.";
   if (mLine.Count() < 2)
      mLine = Polyline();

   mStatus = std::to_string(mSegments) + " segments, " + std::to_string(mStrands) +
             " branches, " + std::to_string(mMesh.indices.size() / 3) + " triangles";
   if (cur.empty())
      mStatus = "Empty axiom.";

   mBuiltAxiom = axiom; mBuiltRules = rules; mBuiltIterations = iterations;
   mBuiltSides = sides; mBuiltAngle = angle; mBuiltLength = length;
   mBuiltLengthScale = lengthScale; mBuiltRadius = radius;
   mBuiltRadiusScale = radiusScale; mBuiltTaper = taper;
   mHasBuilt = true;
   mRevision = NextMeshRevision();
}

const Mesh& LSystemNode::GetMesh()
{
   if (bypassed)
      return EmptyMesh();
   RebuildIfNeeded();
   return mMesh;
}

unsigned long long LSystemNode::MeshRevision()
{
   if (bypassed)
      return 0;
   RebuildIfNeeded();
   return mRevision;
}

const Polyline& LSystemNode::GetPolyline()
{
   RebuildIfNeeded();
   return mLine;
}

Mat4 LSystemNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(uniformScale, uniformScale, uniformScale);
   return Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
}

Material LSystemNode::GetMaterial() const
{
   Material m;
   m.color[0] = color[0]; m.color[1] = color[1]; m.color[2] = color[2];
   m.metallic = metallic;
   m.roughness = roughness;
   m.opacity = opacity;
   m.shading = shading;
   m.emissionColor[0] = emissionColor[0];
   m.emissionColor[1] = emissionColor[1];
   m.emissionColor[2] = emissionColor[2];
   m.emission = emission;
   return m;
}

unsigned long long LSystemNode::MaterialRevision() const
{
   return ComputeContentRevision(GetMaterial(), mMaterialRevision, mLastMaterialHash);
}

void LSystemNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;
   RebuildIfNeeded();
}
