#include "ProjectionNode.h"

#include <OpenGL/gl3.h>
#include <algorithm>
#include <cmath>

namespace
{
   const std::vector<std::string> kPatternNames = {
      "Off (Live Input)",
      "Grid",
      "Crosshairs & Diagonals",
      "Color Bars",
      "Combined Target"
   };

   const int kGridSubdiv = 32; // 32x32 quad mesh

   const char* kVertSrc =
      "#version 150\n"
      "in vec2 aPos;\n"
      "in vec2 aUv;\n"
      "out vec2 vUv;\n"
      "void main() {\n"
      "   vUv = aUv;\n"
      "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
      "}\n";

   const char* kFragSrc =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uSrc;\n"
      "uniform int uPatternMode;\n"
      "uniform int uHasInput;\n"
      "\n"
      "void main() {\n"
      "   if (uPatternMode == 1) {\n" // Grid
      "      vec2 g = abs(fract(vUv * 10.0 - 0.5) - 0.5) / max(fwidth(vUv * 10.0), vec2(0.001));\n"
      "      float line = min(g.x, g.y);\n"
      "      float c = 1.0 - min(line, 1.0);\n"
      "      float border = step(0.99, max(vUv.x, max(vUv.y, max(1.0 - vUv.x, 1.0 - vUv.y))));\n"
      "      fragColor = vec4(mix(vec3(0.05, 0.05, 0.08), vec3(0.9, 0.9, 0.95), max(c, border)), 1.0);\n"
      "   } else if (uPatternMode == 2) {\n" // Crosshairs & Diagonals
      "      vec2 d = abs(vUv - 0.5) / max(fwidth(vUv), vec2(0.001));\n"
      "      float crosshair = 1.0 - min(min(d.x, d.y), 1.0);\n"
      "      float diag1 = 1.0 - min(abs(vUv.x - vUv.y) / max(length(fwidth(vUv)), 0.001), 1.0);\n"
      "      float diag2 = 1.0 - min(abs(vUv.x - (1.0 - vUv.y)) / max(length(fwidth(vUv)), 0.001), 1.0);\n"
      "      float pattern = max(crosshair, max(diag1, diag2));\n"
      "      float border = step(0.99, max(vUv.x, max(vUv.y, max(1.0 - vUv.x, 1.0 - vUv.y))));\n"
      "      fragColor = vec4(mix(vec3(0.05, 0.05, 0.08), vec3(0.1, 0.9, 0.4), max(pattern, border)), 1.0);\n"
      "   } else if (uPatternMode == 3) {\n" // Color Bars
      "      int bar = int(clamp(floor(vUv.x * 8.0), 0.0, 7.0));\n"
      "      vec3 col = vec3(0.0);\n"
      "      if (bar == 0) col = vec3(0.9, 0.9, 0.9);\n"
      "      else if (bar == 1) col = vec3(0.9, 0.9, 0.0);\n"
      "      else if (bar == 2) col = vec3(0.0, 0.9, 0.9);\n"
      "      else if (bar == 3) col = vec3(0.0, 0.9, 0.0);\n"
      "      else if (bar == 4) col = vec3(0.9, 0.0, 0.9);\n"
      "      else if (bar == 5) col = vec3(0.9, 0.0, 0.0);\n"
      "      else if (bar == 6) col = vec3(0.0, 0.0, 0.9);\n"
      "      else col = vec3(0.05, 0.05, 0.05);\n"
      "      fragColor = vec4(col, 1.0);\n"
      "   } else if (uPatternMode == 4) {\n" // Combined
      "      vec2 g = abs(fract(vUv * 10.0 - 0.5) - 0.5) / max(fwidth(vUv * 10.0), vec2(0.001));\n"
      "      float grid = 1.0 - min(min(g.x, g.y), 1.0);\n"
      "      vec2 d = abs(vUv - 0.5) / max(fwidth(vUv), vec2(0.001));\n"
      "      float crosshair = 1.0 - min(min(d.x, d.y), 1.0);\n"
      "      float dist = length((vUv - 0.5) * 2.0);\n"
      "      float circle = 1.0 - min(abs(dist - 0.7) / max(length(fwidth(vUv)), 0.001), 1.0);\n"
      "      float border = step(0.99, max(vUv.x, max(vUv.y, max(1.0 - vUv.x, 1.0 - vUv.y))));\n"
      "      float pat = max(max(grid * 0.5, crosshair), max(circle, border));\n"
      "      fragColor = vec4(mix(vec3(0.05, 0.05, 0.08), vec3(1.0, 1.0, 1.0), pat), 1.0);\n"
      "   } else if (uHasInput != 0) {\n"
      "      fragColor = texture(uSrc, vUv);\n"
      "   } else {\n"
      "      vec2 g = abs(fract(vUv * 8.0 - 0.5) - 0.5) / max(fwidth(vUv * 8.0), vec2(0.001));\n"
      "      float grid = 1.0 - min(min(g.x, g.y), 1.0);\n"
      "      float border = step(0.99, max(vUv.x, max(vUv.y, max(1.0 - vUv.x, 1.0 - vUv.y))));\n"
      "      fragColor = vec4(mix(vec3(0.08, 0.08, 0.12), vec3(0.6, 0.7, 0.85), max(grid * 0.4, border)), 1.0);\n"
      "   }\n"
      "}\n";
}

const std::vector<std::string>& ProjectionNode::PatternNames()
{
   return kPatternNames;
}

ProjectionNode::ProjectionNode()
{
   ResetAllPoints();
}

ProjectionNode::~ProjectionNode()
{
   GLUtil::DestroyFbo(mOut);
   if (mProgram != 0)
      glDeleteProgram(mProgram);
   if (mVao != 0)
      glDeleteVertexArrays(1, &mVao);
   if (mVbo != 0)
      glDeleteBuffers(1, &mVbo);
   if (mEbo != 0)
      glDeleteBuffers(1, &mEbo);
}

void ProjectionNode::SetGridSize(int newW, int newH)
{
   newW = std::max(2, std::min(8, newW));
   newH = std::max(2, std::min(8, newH));
   if (newW == gridW && newH == gridH)
      return;

   Point oldPoints[8][8];
   std::memcpy(oldPoints, points, sizeof(points));
   int oldW = std::max(2, std::min(8, gridW));
   int oldH = std::max(2, std::min(8, gridH));

   gridW = newW;
   gridH = newH;

   for (int r = 0; r < newH; ++r)
   {
      float v = (newH > 1) ? (float)r / (float)(newH - 1) : 0.0f;
      float gy = v * (float)(oldH - 1);
      int r0 = std::max(0, std::min(oldH - 2, (int)std::floor(gy)));
      float fy = gy - (float)r0;

      for (int c = 0; c < newW; ++c)
      {
         float u = (newW > 1) ? (float)c / (float)(newW - 1) : 0.0f;
         float gx = u * (float)(oldW - 1);
         int c0 = std::max(0, std::min(oldW - 2, (int)std::floor(gx)));
         float fx = gx - (float)c0;

         Point q00 = oldPoints[r0][c0];
         Point q10 = oldPoints[r0][c0 + 1];
         Point q01 = oldPoints[r0 + 1][c0];
         Point q11 = oldPoints[r0 + 1][c0 + 1];

         points[r][c].x = (1.0f - fx) * (1.0f - fy) * q00.x + fx * (1.0f - fy) * q10.x +
                          (1.0f - fx) * fy * q01.x + fx * fy * q11.x;
         points[r][c].y = (1.0f - fx) * (1.0f - fy) * q00.y + fx * (1.0f - fy) * q10.y +
                          (1.0f - fx) * fy * q01.y + fx * fy * q11.y;
      }
   }
}

void ProjectionNode::ResetCorners()
{
   points[0][0] = { 0.0f, 0.0f }; // Top-Left
   points[0][1] = { 1.0f, 0.0f }; // Top-Right
   points[1][1] = { 1.0f, 1.0f }; // Bottom-Right
   points[1][0] = { 0.0f, 1.0f }; // Bottom-Left
}

void ProjectionNode::ResetAllPoints()
{
   int gw = std::max(2, std::min(8, gridW));
   int gh = std::max(2, std::min(8, gridH));
   for (int r = 0; r < 8; ++r)
   {
      for (int c = 0; c < 8; ++c)
      {
         float nx = (c < gw) ? ((gw > 1) ? (float)c / (float)(gw - 1) : 0.0f) : ((float)c / 7.0f);
         float ny = (r < gh) ? ((gh > 1) ? (float)r / (float)(gh - 1) : 0.0f) : ((float)r / 7.0f);
         points[r][c].x = nx;
         points[r][c].y = ny;
      }
   }
   ResetCorners();
}

void ProjectionNode::FlipH()
{
   if (mode == (int)WarpMode::CornerPin)
   {
      std::swap(points[0][0], points[0][1]);
      std::swap(points[1][0], points[1][1]);
   }
   else
   {
      int gw = std::max(2, std::min(8, gridW));
      int gh = std::max(2, std::min(8, gridH));
      for (int r = 0; r < gh; ++r)
         for (int c = 0; c < gw / 2; ++c)
            std::swap(points[r][c], points[r][gw - 1 - c]);
   }
}

void ProjectionNode::FlipV()
{
   if (mode == (int)WarpMode::CornerPin)
   {
      std::swap(points[0][0], points[1][0]);
      std::swap(points[0][1], points[1][1]);
   }
   else
   {
      int gw = std::max(2, std::min(8, gridW));
      int gh = std::max(2, std::min(8, gridH));
      for (int r = 0; r < gh / 2; ++r)
         for (int c = 0; c < gw; ++c)
            std::swap(points[r][c], points[gh - 1 - r][c]);
   }
}

bool ProjectionNode::EnsureShader()
{
   if (mShaderTried)
      return mProgram != 0;
   mShaderTried = true;

   unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(vs, 1, &kVertSrc, nullptr);
   glCompileShader(vs);

   unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(fs, 1, &kFragSrc, nullptr);
   glCompileShader(fs);

   mProgram = glCreateProgram();
   glAttachShader(mProgram, vs);
   glAttachShader(mProgram, fs);
   glBindAttribLocation(mProgram, 0, "aPos");
   glBindAttribLocation(mProgram, 1, "aUv");
   glLinkProgram(mProgram);

   glDeleteShader(vs);
   glDeleteShader(fs);

   int linked = 0;
   glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
   if (!linked)
   {
      glDeleteProgram(mProgram);
      mProgram = 0;
   }
   return mProgram != 0;
}

void ProjectionNode::EnsureMesh()
{
   if (mVao != 0)
      return;

   glGenVertexArrays(1, &mVao);
   glGenBuffers(1, &mVbo);
   glGenBuffers(1, &mEbo);

   std::vector<unsigned int> indices;
   indices.reserve(kGridSubdiv * kGridSubdiv * 6);
   for (int j = 0; j < kGridSubdiv; ++j)
   {
      for (int i = 0; i < kGridSubdiv; ++i)
      {
         unsigned int row1 = j * (kGridSubdiv + 1);
         unsigned int row2 = (j + 1) * (kGridSubdiv + 1);

         indices.push_back(row1 + i);
         indices.push_back(row2 + i);
         indices.push_back(row1 + i + 1);

         indices.push_back(row1 + i + 1);
         indices.push_back(row2 + i);
         indices.push_back(row2 + i + 1);
      }
   }
   mIndexCount = (int)indices.size();

   glBindVertexArray(mVao);
   glBindBuffer(GL_ARRAY_BUFFER, mVbo);
   // Allocate buffer space for (kGridSubdiv+1)*(kGridSubdiv+1) vertices * 4 floats (pos.xy, uv.xy)
   int vertCount = (kGridSubdiv + 1) * (kGridSubdiv + 1);
   glBufferData(GL_ARRAY_BUFFER, vertCount * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mEbo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

   glBindVertexArray(0);
}

void ProjectionNode::UpdateMeshVertices()
{
   EnsureMesh();

   // Corner points in [0, 1] UI space (0,0 is top-left, 1,1 is bottom-right)
   Point p0 = points[0][0]; // Top-Left
   Point p1 = points[0][1]; // Top-Right
   Point p2 = points[1][1]; // Bottom-Right
   Point p3 = points[1][0]; // Bottom-Left

   // Compute DLT Homography matrix H
   float dx1 = p1.x - p2.x;
   float dx2 = p3.x - p2.x;
   float sx  = p0.x - p1.x + p2.x - p3.x;
   float dy1 = p1.y - p2.y;
   float dy2 = p3.y - p2.y;
   float sy  = p0.y - p1.y + p2.y - p3.y;

   float det = dx1 * dy2 - dx2 * dy1;
   bool useHomography = (mode == (int)WarpMode::CornerPin) && (std::abs(det) > 1e-7f);
   float g = 0.0f, h = 0.0f;
   float a11 = 0.0f, a12 = 0.0f, a13 = 0.0f;
   float a21 = 0.0f, a22 = 0.0f, a23 = 0.0f;

   if (useHomography)
   {
      g = (sx * dy2 - sy * dx2) / det;
      h = (dx1 * sy - dy1 * sx) / det;
      a11 = p1.x - p0.x + g * p1.x;
      a12 = p3.x - p0.x + h * p3.x;
      a13 = p0.x;
      a21 = p1.y - p0.y + g * p1.y;
      a22 = p3.y - p0.y + h * p3.y;
      a23 = p0.y;
   }

   int gw = std::max(2, std::min(8, gridW));
   int gh = std::max(2, std::min(8, gridH));

   std::vector<float> verts;
   verts.reserve((kGridSubdiv + 1) * (kGridSubdiv + 1) * 4);

   for (int j = 0; j <= kGridSubdiv; ++j)
   {
      float v = (float)j / (float)kGridSubdiv;
      for (int i = 0; i <= kGridSubdiv; ++i)
      {
         float u = (float)i / (float)kGridSubdiv;
         float X = 0.0f, Y = 0.0f;

         if (mode == (int)WarpMode::CornerPin)
         {
            if (useHomography)
            {
               float W = g * u + h * v + 1.0f;
               if (std::abs(W) > 1e-5f)
               {
                  X = (a11 * u + a12 * v + a13) / W;
                  Y = (a21 * u + a22 * v + a23) / W;
               }
               else
               {
                  X = (1.0f - u) * (1.0f - v) * p0.x + u * (1.0f - v) * p1.x + u * v * p2.x + (1.0f - u) * v * p3.x;
                  Y = (1.0f - u) * (1.0f - v) * p0.y + u * (1.0f - v) * p1.y + u * v * p2.y + (1.0f - u) * v * p3.y;
               }
            }
            else
            {
               X = (1.0f - u) * (1.0f - v) * p0.x + u * (1.0f - v) * p1.x + u * v * p2.x + (1.0f - u) * v * p3.x;
               Y = (1.0f - u) * (1.0f - v) * p0.y + u * (1.0f - v) * p1.y + u * v * p2.y + (1.0f - u) * v * p3.y;
            }
         }
         else
         {
            // Bilinear mesh grid interpolation
            float gx = u * (float)(gw - 1);
            float gy = v * (float)(gh - 1);
            int c0 = std::max(0, std::min(gw - 2, (int)std::floor(gx)));
            int r0 = std::max(0, std::min(gh - 2, (int)std::floor(gy)));
            float fx = gx - (float)c0;
            float fy = gy - (float)r0;

            Point q00 = points[r0][c0];
            Point q10 = points[r0][c0 + 1];
            Point q01 = points[r0 + 1][c0];
            Point q11 = points[r0 + 1][c0 + 1];

            X = (1.0f - fx) * (1.0f - fy) * q00.x + fx * (1.0f - fy) * q10.x +
                (1.0f - fx) * fy * q01.x + fx * fy * q11.x;
            Y = (1.0f - fx) * (1.0f - fy) * q00.y + fx * (1.0f - fy) * q10.y +
                (1.0f - fx) * fy * q01.y + fx * fy * q11.y;
         }

         // Map X, Y in [0, 1] screen space to OpenGL clip space [-1, 1]
         float clipX = X * 2.0f - 1.0f;
         float clipY = 1.0f - Y * 2.0f;

         // Texture UV (v is inverted in OpenGL texture coords)
         float texU = u;
         float texV = 1.0f - v;

         verts.push_back(clipX);
         verts.push_back(clipY);
         verts.push_back(texU);
         verts.push_back(texV);
      }
   }

   glBindBuffer(GL_ARRAY_BUFFER, mVbo);
   glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
   glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ProjectionNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShader())
      return;

   unsigned int srcTex = mInput.Pull(frameId);
   bool hasInput = (srcTex != 0);

   int targetW = (int)std::max(16.0f, width);
   int targetH = (int)std::max(16.0f, height);
   if (matchInput && hasInput)
   {
      int inW = mInput.GetSource()->GetOutputWidth();
      int inH = mInput.GetSource()->GetOutputHeight();
      if (inW > 0 && inH > 0)
      {
         targetW = inW;
         targetH = inH;
      }
   }

   if (!GLUtil::EnsureFbo(mOut, targetW, targetH))
      return;

   // Signature caching check
   Signature sig;
   sig.upstreamRev = mInput.GetSource() ? mInput.GetSource()->TextureRevision() : 0;
   sig.width = targetW;
   sig.height = targetH;
   sig.matchInput = matchInput;
   sig.mode = mode;
   sig.patternMode = patternMode;
   sig.gridW = gridW;
   sig.gridH = gridH;
   sig.hasInput = hasInput;
   int ptIdx = 0;
   for (int r = 0; r < 8; ++r)
   {
      for (int c = 0; c < 8; ++c)
      {
         sig.points[ptIdx++] = points[r][c].x;
         sig.points[ptIdx++] = points[r][c].y;
      }
   }

   if (mHasBuilt && mBuilt == sig)
      return;

   mBuilt = sig;
   mHasBuilt = true;
   ++mRevision;

   UpdateMeshVertices();

   GLint prevFbo = 0;
   GLint prevVp[4];
   glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
   glGetIntegerv(GL_VIEWPORT, prevVp);

   glBindFramebuffer(GL_FRAMEBUFFER, mOut.fbo);
   glViewport(0, 0, mOut.w, mOut.h);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   glUseProgram(mProgram);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, srcTex);
   glUniform1i(glGetUniformLocation(mProgram, "uSrc"), 0);
   glUniform1i(glGetUniformLocation(mProgram, "uPatternMode"), patternMode);
   glUniform1i(glGetUniformLocation(mProgram, "uHasInput"), hasInput ? 1 : 0);

   glBindVertexArray(mVao);
   glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, (void*)0);
   glBindVertexArray(0);

   glUseProgram(0);
   glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
   glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}
