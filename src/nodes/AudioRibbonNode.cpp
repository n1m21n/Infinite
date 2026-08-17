#include "AudioRibbonNode.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// AudioRibbonAudioSink: Audio Thread Sink
// ---------------------------------------------------------------------------
class AudioRibbonAudioSink : public AudioNode
{
public:
   AudioRibbonAudioSink() = default;
   ~AudioRibbonAudioSink() override = default;

   void ProcessBlock(const AudioBuffer* const* inputs, int numInputs, AudioBuffer& output) override
   {
      const int numFrames = output.numFrames;
      if (numFrames <= 0)
         return;

      for (int ch = 0; ch < output.numChannels; ch++)
      {
         if (output.channels[ch])
            std::fill_n(output.channels[ch], numFrames, 0.0f);
      }

      const AudioBuffer* inBuf = nullptr;
      if (inputs != nullptr)
      {
         for (int s = 0; s < numInputs; s++)
         {
            if (inputs[s] != nullptr && inputs[s]->channels[0] != nullptr)
            {
               inBuf = inputs[s];
               break;
            }
         }
      }

      if (inBuf != nullptr)
      {
         const float* inL = inBuf->channels[0];
         const float* inR = (inBuf->numChannels > 1 && inBuf->channels[1] != nullptr) ? inBuf->channels[1] : inL;

         std::vector<float> mono(numFrames);
         for (int i = 0; i < numFrames; i++)
         {
            mono[i] = (inL[i] + inR[i]) * 0.5f;
            if (output.channels[0]) output.channels[0][i] = inL[i];
            if (output.numChannels > 1 && output.channels[1]) output.channels[1][i] = inR[i];
         }
         mMeterRing.Write(mono.data(), numFrames);
      }
   }

   int ReadSamples(float* out, int capacity)
   {
      return mMeterRing.Read(out, capacity);
   }

private:
   MeterRing mMeterRing;
};

// ---------------------------------------------------------------------------
// AudioRibbonNode (Main Thread Terminal IGeometrySource)
// ---------------------------------------------------------------------------
AudioRibbonNode::AudioRibbonNode()
{
   mAudioSink = std::make_unique<AudioRibbonAudioSink>();
   mAudioWaveform.assign(1024, 0.0f);
   mSmoothedWaveform.assign(1024, 0.0f);
   RebuildRibbon();
}

AudioRibbonNode::~AudioRibbonNode() = default;

AudioNode* AudioRibbonNode::GetAudioNode()
{
   if (!mAudioSink)
      mAudioSink = std::make_unique<AudioRibbonAudioSink>();
   return mAudioSink.get();
}

const Mesh& AudioRibbonNode::GetMesh()
{
   return mMesh;
}

unsigned long long AudioRibbonNode::MeshRevision()
{
   return mMeshRevision;
}

Mat4 AudioRibbonNode::GetModelMatrix() const
{
   Mat4 m = Mat4::Scale(scaleX * uniformScale, scaleY * uniformScale, scaleZ * uniformScale);
   m = Mat4::Multiply(Mat4::RotationZ(rotZ), m);
   m = Mat4::Multiply(Mat4::RotationY(rotY), m);
   m = Mat4::Multiply(Mat4::RotationX(rotX), m);
   m = Mat4::Multiply(Mat4::Translation(posX, posY, posZ), m);
   return m;
}

Material AudioRibbonNode::GetMaterial() const
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
   m.ior = ior;
   return m;
}

void AudioRibbonNode::RebuildRibbon()
{
   const int segs = std::clamp(segments, 4, 2048);
   mMesh.vertices.clear();
   mMesh.indices.clear();
   mMesh.faceMask.clear();
   mMesh.selectionGroup.clear();
   mMesh.vertexColor.clear();

   mMesh.vertices.reserve((size_t)(segs + 1) * 2);
   mMesh.indices.reserve((size_t)segs * 6);

   const int nSamples = (int)mSmoothedWaveform.size();
   const float halfWidth = width * 0.5f;

   for (int i = 0; i <= segs; i++)
   {
      const float t = (float)i / (float)segs;
      const float x = -1.0f + 2.0f * t;

      float s = 0.0f;
      if (nSamples > 0)
      {
         const int idx = std::clamp((int)(t * (float)(nSamples - 1)), 0, nSamples - 1);
         s = mSmoothedWaveform[idx] * gain;
      }
      const float y = s * heightScale;

      // Tangent / normal computation in XY
      const float tPrev = std::max(0.0f, (float)(i - 1) / (float)segs);
      const float tNext = std::min(1.0f, (float)(i + 1) / (float)segs);
      const float xPrev = -1.0f + 2.0f * tPrev;
      const float xNext = -1.0f + 2.0f * tNext;

      float sPrev = 0.0f, sNext = 0.0f;
      if (nSamples > 0)
      {
         const int idxPrev = std::clamp((int)(tPrev * (float)(nSamples - 1)), 0, nSamples - 1);
         const int idxNext = std::clamp((int)(tNext * (float)(nSamples - 1)), 0, nSamples - 1);
         sPrev = mSmoothedWaveform[idxPrev] * gain;
         sNext = mSmoothedWaveform[idxNext] * gain;
      }
      const float yPrev = sPrev * heightScale;
      const float yNext = sNext * heightScale;

      const float dx = xNext - xPrev;
      const float dy = yNext - yPrev;
      const float len = sqrtf(dx * dx + dy * dy);

      float nx = 0.0f, ny = 1.0f, nz = 0.0f;
      if (len > 1e-6f)
      {
         nx = -dy / len;
         ny = dx / len;
      }

      Vertex v0;
      v0.px = x; v0.py = y; v0.pz = -halfWidth;
      v0.nx = nx; v0.ny = ny; v0.nz = nz;
      v0.u = t; v0.v = 0.0f;

      Vertex v1;
      v1.px = x; v1.py = y; v1.pz = halfWidth;
      v1.nx = nx; v1.ny = ny; v1.nz = nz;
      v1.u = t; v1.v = 1.0f;

      mMesh.vertices.push_back(v0);
      mMesh.vertices.push_back(v1);
   }

   for (int i = 0; i < segs; i++)
   {
      const unsigned int i0 = (unsigned int)(i * 2);
      const unsigned int i1 = (unsigned int)(i * 2 + 1);
      const unsigned int i2 = (unsigned int)((i + 1) * 2);
      const unsigned int i3 = (unsigned int)((i + 1) * 2 + 1);

      mMesh.indices.push_back(i0);
      mMesh.indices.push_back(i2);
      mMesh.indices.push_back(i1);

      mMesh.indices.push_back(i1);
      mMesh.indices.push_back(i2);
      mMesh.indices.push_back(i3);
   }
}

void AudioRibbonNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   const bool paramsChanged = (width != mLastWidth ||
                               heightScale != mLastHeightScale ||
                               segments != mLastSegments ||
                               gain != mLastGain ||
                               smoothing != mLastSmoothing);

   bool audioUpdated = false;

   if (mAudioInput.IsConnected())
   {
      const int winSize = 1024;
      if ((int)mAudioWaveform.size() != winSize)
         mAudioWaveform.assign(winSize, 0.0f);
      if ((int)mSmoothedWaveform.size() != winSize)
         mSmoothedWaveform.assign(winSize, 0.0f);

      float tempBuf[4096];
      const int readCount = mAudioSink ? mAudioSink->ReadSamples(tempBuf, winSize) : 0;
      if (readCount > 0)
      {
         audioUpdated = true;
         if (readCount >= winSize)
         {
            mAudioWaveform.assign(tempBuf + (readCount - winSize), tempBuf + readCount);
         }
         else
         {
            const int keep = winSize - readCount;
            std::copy(mAudioWaveform.begin() + readCount, mAudioWaveform.end(), mAudioWaveform.begin());
            std::copy(tempBuf, tempBuf + readCount, mAudioWaveform.begin() + keep);
         }

         const float sWeight = std::clamp(smoothing, 0.0f, 0.99f);
         for (int i = 0; i < winSize; i++)
         {
            mSmoothedWaveform[i] = mSmoothedWaveform[i] * sWeight + mAudioWaveform[i] * (1.0f - sWeight);
         }
      }
   }

   if (audioUpdated || paramsChanged)
   {
      RebuildRibbon();
      mMeshRevision = NextMeshRevision();

      mLastWidth = width;
      mLastHeightScale = heightScale;
      mLastSegments = segments;
      mLastGain = gain;
      mLastSmoothing = smoothing;
   }
}
