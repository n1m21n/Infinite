#include "DrawNode.h"

#include "gl3.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "Transport.h"

namespace
{
   const std::vector<std::string> kBrushNames = {
      "Soft Round", "Hard Round", "Square", "Spray", "Chalk", "Ink Bleed"
   };

   // One stamp per pass, blended into the canvas. Slower than instancing, but a
   // stroke only produces a handful of stamps per frame at sane spacing.
   const char* kStampFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uCanvas;\n"
      "uniform vec2 uCenter;\n"
      "uniform float uRadius;\n"
      "uniform float uAspect;\n"
      "uniform int uBrush;\n"
      "uniform float uHardness;\n"
      "uniform float uOpacity;\n"
      "uniform vec3 uColor;\n"
      "uniform int uErase;\n"
      "uniform float uSeed;\n"
      "float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)) + uSeed) * 43758.5453); }\n"
      "void main() {\n"
      "   vec4 dst = texture(uCanvas, vUv);\n"
      "   vec2 d = vUv - uCenter;\n"
      "   d.x *= uAspect;\n"
      "   float r = length(d) / max(uRadius, 1e-4);\n"
      "\n"
      "   float a = 0.0;\n"
      "   if (uBrush == 0) {\n"                       // Soft Round
      "      a = 1.0 - smoothstep(uHardness, 1.0, r);\n"
      "   } else if (uBrush == 1) {\n"                // Hard Round
      "      a = 1.0 - smoothstep(0.94, 1.0, r);\n"
      "   } else if (uBrush == 2) {\n"                // Square
      "      vec2 q = abs(d) / max(uRadius, 1e-4);\n"
      "      float m = max(q.x, q.y);\n"
      "      a = 1.0 - smoothstep(uHardness, 1.0, m);\n"
      "   } else if (uBrush == 3) {\n"                // Spray
      "      float n = hash(floor(vUv * 900.0));\n"
      "      a = (1.0 - smoothstep(0.0, 1.0, r)) * step(0.55, n);\n"
      "   } else if (uBrush == 4) {\n"                // Chalk
      "      float n = hash(floor(vUv * 500.0)) * 0.55 + 0.45;\n"
      "      a = (1.0 - smoothstep(uHardness * 0.6, 1.0, r)) * n;\n"
      "   } else {\n"                                 // Ink Bleed
      "      float n = hash(floor(vUv * 120.0)) * 0.35;\n"
      "      a = 1.0 - smoothstep(uHardness - n, 1.0 + n, r);\n"
      "   }\n"
      "   a = clamp(a, 0.0, 1.0) * uOpacity;\n"
      "   if (a <= 0.0) { fragColor = dst; return; }\n"
      "\n"
      "   if (uErase == 1) {\n"
      "      fragColor = vec4(dst.rgb, dst.a * (1.0 - a));\n"
      "      return;\n"
      "   }\n"
      "   // standard source-over onto a premultiplied-ish straight-alpha canvas\n"
      "   float outA = a + dst.a * (1.0 - a);\n"
      "   vec3 outRgb = (uColor * a + dst.rgb * dst.a * (1.0 - a)) / max(outA, 1e-4);\n"
      "   fragColor = vec4(outRgb, outA);\n"
      "}\n";

   const char* kCompositeFrag =
      "#version 150\n"
      "in vec2 vUv;\n"
      "out vec4 fragColor;\n"
      "uniform sampler2D uBase;\n"
      "uniform sampler2D uPaint;\n"
      "uniform int uHasBase;\n"
      "void main() {\n"
      "   vec4 paint = texture(uPaint, vUv);\n"
      "   if (uHasBase == 0) { fragColor = paint; return; }\n"
      "   vec4 base = texture(uBase, vUv);\n"
      "   float outA = paint.a + base.a * (1.0 - paint.a);\n"
      "   vec3 rgb = paint.rgb * paint.a + base.rgb * base.a * (1.0 - paint.a);\n"
      "   fragColor = vec4(rgb / max(outA, 1e-4), outA);\n"
      "}\n";
}

const std::vector<std::string>& DrawNode::BrushNames()
{
   return kBrushNames;
}

DrawNode::~DrawNode()
{
   GLUtil::DestroyFbo(mCanvas);
   GLUtil::DestroyFbo(mScratch);
   GLUtil::DestroyFbo(mComposite);
   if (mStampProgram != 0)
      glDeleteProgram(mStampProgram);
   if (mCompositeProgram != 0)
      glDeleteProgram(mCompositeProgram);
}

bool DrawNode::EnsureShaders()
{
   if (mShaderTried)
      return mStampProgram != 0 && mCompositeProgram != 0;
   mShaderTried = true;
   mStampProgram = GLUtil::CompileProgram(kStampFrag);
   mCompositeProgram = GLUtil::CompileProgram(kCompositeFrag);
   return mStampProgram != 0 && mCompositeProgram != 0;
}

DrawNode::Stamp DrawNode::MakeStamp(float x, float y, float seed) const
{
   Stamp s;
   s.x = x;
   s.y = y;
   s.seed = seed;
   s.size = brushSize;
   s.opacity = opacity;
   s.hardness = hardness;
   s.brush = brush;
   s.erase = eraser;
   s.color[0] = color[0];
   s.color[1] = color[1];
   s.color[2] = color[2];
   return s;
}

// Delta-encoded: "v1" then a stream of terms. A "B..." term updates the
// running brush state (emitted only when it changes); any other term is a
// stamp inheriting that state. Keeping brush state out of most stamps is
// what keeps a real drawing (tens of thousands of stamps) from producing a
// multi-megabyte patch line - see the comment on VisitParams.
std::string DrawNode::EncodeRecording(const std::vector<RecordedStamp>& recorded)
{
   std::string out = "v1";
   Stamp running;
   bool haveState = false;
   char buf[160];
   for (const RecordedStamp& r : recorded)
   {
      const Stamp& s = r.stamp;
      const bool changed = !haveState ||
         s.brush != running.brush || s.size != running.size || s.opacity != running.opacity ||
         s.hardness != running.hardness || s.erase != running.erase ||
         s.color[0] != running.color[0] || s.color[1] != running.color[1] || s.color[2] != running.color[2];
      if (changed)
      {
         snprintf(buf, sizeof(buf), ";B%d,%.6g,%.6g,%.6g,%d,%.6g,%.6g,%.6g",
                  s.brush, (double)s.size, (double)s.opacity, (double)s.hardness,
                  s.erase ? 1 : 0, (double)s.color[0], (double)s.color[1], (double)s.color[2]);
         out += buf;
         running = s;
         haveState = true;
      }
      snprintf(buf, sizeof(buf), ";%.6g,%.6g,%.6g,%.9g", (double)s.x, (double)s.y, (double)s.seed, r.beat);
      out += buf;
   }
   return out;
}

std::vector<DrawNode::RecordedStamp> DrawNode::DecodeRecording(const std::string& str)
{
   std::vector<RecordedStamp> recorded;
   Stamp running;
   bool haveState = false;
   bool first = true;
   size_t start = 0;
   while (start <= str.size())
   {
      const size_t sep = str.find(';', start);
      const std::string term = str.substr(start, sep == std::string::npos ? std::string::npos : sep - start);

      if (first)
      {
         first = false; // version tag, ignored - nothing to parse yet
      }
      else if (!term.empty() && term[0] == 'B')
      {
         const std::string body = term.substr(1);
         std::vector<std::string> parts;
         size_t p = 0;
         while (p <= body.size())
         {
            const size_t c = body.find(',', p);
            parts.push_back(body.substr(p, c == std::string::npos ? std::string::npos : c - p));
            if (c == std::string::npos)
               break;
            p = c + 1;
         }
         if (parts.size() >= 8)
         {
            Stamp s;
            s.brush = atoi(parts[0].c_str());
            s.size = (float)atof(parts[1].c_str());
            s.opacity = (float)atof(parts[2].c_str());
            s.hardness = (float)atof(parts[3].c_str());
            s.erase = atoi(parts[4].c_str()) != 0;
            s.color[0] = (float)atof(parts[5].c_str());
            s.color[1] = (float)atof(parts[6].c_str());
            s.color[2] = (float)atof(parts[7].c_str());
            running = s;
            haveState = true;
         }
         // malformed brush term: skip it, keep the previous running state
      }
      else if (!term.empty() && haveState)
      {
         const size_t c1 = term.find(',');
         const size_t c2 = c1 == std::string::npos ? std::string::npos : term.find(',', c1 + 1);
         const size_t c3 = c2 == std::string::npos ? std::string::npos : term.find(',', c2 + 1);
         if (c1 != std::string::npos && c2 != std::string::npos && c3 != std::string::npos)
         {
            RecordedStamp r;
            r.stamp = running;
            r.stamp.x = (float)atof(term.substr(0, c1).c_str());
            r.stamp.y = (float)atof(term.substr(c1 + 1, c2 - c1 - 1).c_str());
            r.stamp.seed = (float)atof(term.substr(c2 + 1, c3 - c2 - 1).c_str());
            r.beat = atof(term.substr(c3 + 1).c_str());
            recorded.push_back(r);
         }
         // malformed stamp term: skip it
      }

      if (sep == std::string::npos)
         break;
      start = sep + 1;
   }
   return recorded;
}

void DrawNode::StartRecording()
{
   mRecorded.clear();
   mRecording = true;
   mPlaying = false;
   mRecordStartBeat = Transport::Instance().Beats();
}

void DrawNode::StopRecording()
{
   mRecording = false;
}

void DrawNode::PlayRecording()
{
   if (mRecorded.empty())
      return;
   mPlaying = true;
   mRecording = false;
   mPlayStartBeat = Transport::Instance().Beats();
   mPlayIndex = 0;
   mPlayhead = 0.0;
   mNeedsClear = true; // replay draws onto a fresh canvas
}

void DrawNode::StopPlayback()
{
   mPlaying = false;
}

void DrawNode::ClearRecording()
{
   mRecorded.clear();
   mRecording = false;
   mPlaying = false;
   mPlayIndex = 0;
}

void DrawNode::BeginStroke(float x, float y)
{
   mStrokeActive = true;
   mLastX = x;
   mLastY = y;
   mSeedCounter += 3.77f;
   const Stamp s = MakeStamp(x, y, mSeedCounter);
   mPending.push_back(s);
   if (mRecording && mRecorded.size() < kMaxRecordedStamps)
      mRecorded.push_back({ s, Transport::Instance().Beats() - mRecordStartBeat });
   mStrokeCount++;
}

void DrawNode::ContinueStroke(float x, float y)
{
   if (!mStrokeActive)
   {
      BeginStroke(x, y);
      return;
    }

   // Walk along the segment dropping stamps at a fixed spacing, so a fast drag
   // still paints a continuous line rather than dotted islands.
   const float step = std::max(0.002f, brushSize * std::max(0.02f, spacing));
   float dx = x - mLastX;
   float dy = y - mLastY;
   float dist = std::sqrt(dx * dx + dy * dy);
   if (dist < step)
      return;

   const int steps = std::min(256, (int)(dist / step));
   for (int i = 1; i <= steps; i++)
   {
      const float t = (float)i / (float)steps;
      mSeedCounter += 1.31f;
      float jx = 0.0f, jy = 0.0f;
      if (jitter > 0.0f)
      {
         jx = (std::fmod(std::fabs(std::sin(mSeedCounter * 12.9898f) * 43758.5453f), 1.0f) - 0.5f) * jitter * brushSize;
         jy = (std::fmod(std::fabs(std::sin(mSeedCounter * 78.233f) * 43758.5453f), 1.0f) - 0.5f) * jitter * brushSize;
      }
      const Stamp s = MakeStamp(mLastX + dx * t + jx, mLastY + dy * t + jy, mSeedCounter);
      mPending.push_back(s);
      if (mRecording && mRecorded.size() < kMaxRecordedStamps)
         mRecorded.push_back({ s, Transport::Instance().Beats() - mRecordStartBeat });
   }
   mLastX = x;
   mLastY = y;
   mStrokeCount++;
}

void DrawNode::EndStroke()
{
   mStrokeActive = false;
}

void DrawNode::FlushStamps()
{
   if (mPending.empty())
      return;

   // Each stamp reads the canvas and writes the scratch, then they swap. Reading
   // and writing the same FBO in one pass is undefined, hence the ping-pong.
   const float aspect = (float)mCanvas.w / (float)std::max(1, mCanvas.h);
   for (const Stamp& stamp : mPending)
   {
      const unsigned int canvasTex = GLUtil::FboTexture(mCanvas);
      GLUtil::RunShaderPass(mScratch, mStampProgram, [&]()
      {
         glActiveTexture(GL_TEXTURE0);
         glBindTexture(GL_TEXTURE_2D, canvasTex);
         glUniform1i(glGetUniformLocation(mStampProgram, "uCanvas"), 0);
         glUniform2f(glGetUniformLocation(mStampProgram, "uCenter"), stamp.x, stamp.y);
         glUniform1f(glGetUniformLocation(mStampProgram, "uRadius"), std::max(0.001f, stamp.size * 0.5f));
         glUniform1f(glGetUniformLocation(mStampProgram, "uAspect"), aspect);
         glUniform1i(glGetUniformLocation(mStampProgram, "uBrush"), stamp.brush);
         glUniform1f(glGetUniformLocation(mStampProgram, "uHardness"), std::min(0.98f, stamp.hardness));
         glUniform1f(glGetUniformLocation(mStampProgram, "uOpacity"), stamp.opacity);
         glUniform3f(glGetUniformLocation(mStampProgram, "uColor"), stamp.color[0], stamp.color[1], stamp.color[2]);
         glUniform1i(glGetUniformLocation(mStampProgram, "uErase"), stamp.erase ? 1 : 0);
         glUniform1f(glGetUniformLocation(mStampProgram, "uSeed"), stamp.seed);
      });
      std::swap(mCanvas, mScratch);
   }
   mPending.clear();
}

void DrawNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (!EnsureShaders())
      return;

   unsigned int baseTex = mInput.IsConnected() ? mInput.Pull(frameId) : 0;

   int w, h;
   if (baseTex != 0)
   {
      w = std::max(8, mInput.Width());
      h = std::max(8, mInput.Height());
   }
   else
   {
      w = std::max(8, (int)canvasWidth);
      h = std::max(8, (int)canvasHeight);
   }

   if (mCanvas.w != w || mCanvas.h != h)
      mNeedsClear = true;

   if (!GLUtil::EnsureFbo(mCanvas, w, h))
      return;
   GLUtil::EnsureFbo(mScratch, w, h);
   GLUtil::EnsureFbo(mComposite, w, h);

   auto clearCanvas = [this]()
   {
      // RunShaderPass clears to transparent before drawing; an empty setup gives
      // a wiped canvas without needing a dedicated clear shader.
      GLUtil::RunShaderPass(mCanvas, mCompositeProgram, [this]()
      {
         glUniform1i(glGetUniformLocation(mCompositeProgram, "uHasBase"), 0);
         glActiveTexture(GL_TEXTURE1);
         glBindTexture(GL_TEXTURE_2D, 0);
         glUniform1i(glGetUniformLocation(mCompositeProgram, "uPaint"), 1);
      });
   };

   if (mNeedsClear)
   {
      clearCanvas();
      mNeedsClear = false;
      mStrokeCount = 0;
      mPending.clear();
   }

   if (mPlaying && !mRecorded.empty())
   {
      const double length = std::max(1e-4, RecordedLength());
      mPlayhead = (Transport::Instance().Beats() - mPlayStartBeat) * std::max(0.01f, playSpeed);
      if (mPlayhead > length)
      {
         if (loopPlayback)
         {
            mPlayhead = 0.0;
            mPlayStartBeat = Transport::Instance().Beats();
            mPlayIndex = 0;
            // clear immediately: the clear block already ran this frame, so
            // deferring would paint the new loop's first stamps over the old one
            clearCanvas();
            mStrokeCount = 0;
            mPending.clear();
         }
         else
         {
            mPlaying = false;
         }
      }
      // emit every stamp whose timestamp the playhead has now passed
      while (mPlayIndex < mRecorded.size() && mRecorded[mPlayIndex].beat <= mPlayhead)
      {
         mPending.push_back(mRecorded[mPlayIndex].stamp);
         mPlayIndex++;
      }
   }

   FlushStamps();

   const unsigned int paintTex = GLUtil::FboTexture(mCanvas);
   GLUtil::RunShaderPass(mComposite, mCompositeProgram, [this, baseTex, paintTex]()
   {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, baseTex != 0 ? baseTex : paintTex);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uBase"), 0);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, paintTex);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uPaint"), 1);
      glUniform1i(glGetUniformLocation(mCompositeProgram, "uHasBase"), baseTex != 0 ? 1 : 0);
   });
}
