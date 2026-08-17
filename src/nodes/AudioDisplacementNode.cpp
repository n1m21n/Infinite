#include "AudioDisplacementNode.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MeterRing.h"
#include "audio/dsp/WaveTerrainDsp.h"

namespace
{
   const std::vector<std::string> kModeNames = {
      "Normal Waveform",
      "Spectral FFT Bands",
      "Acoustic Ripples",
      "Cymatics (Chladni)",
      "Directional Axis"
   };

   static const Mesh kEmptyMesh {};
}

const std::vector<std::string>& AudioDisplacementNode::ModeNames()
{
   return kModeNames;
}

// ---------------------------------------------------------------------------
// AudioDisplaceAudioSink: Audio Thread Consumer
// ---------------------------------------------------------------------------
class AudioDisplaceAudioSink : public AudioNode
{
public:
   AudioDisplaceAudioSink()
   {
      mMono.resize(4096, 0.0f);
   }
   ~AudioDisplaceAudioSink() override = default;

   void PrepareToPlay(double /*sampleRate*/, int maxBlockSize) override
   {
      const int allocSize = std::max(maxBlockSize, 4096);
      mMono.resize(allocSize, 0.0f);
   }

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

         if (mMono.size() < (size_t)numFrames)
            mMono.resize(numFrames, 0.0f);

         for (int i = 0; i < numFrames; i++)
         {
            mMono[i] = (inL[i] + inR[i]) * 0.5f;
            if (output.channels[0]) output.channels[0][i] = inL[i];
            if (output.numChannels > 1 && output.channels[1]) output.channels[1][i] = inR[i];
         }
         mMeterRing.Write(mMono.data(), numFrames);
      }
   }

   int ReadSamples(float* out, int capacity)
   {
      return mMeterRing.Read(out, capacity);
   }

private:
   MeterRing mMeterRing;
   std::vector<float> mMono;
};

// ---------------------------------------------------------------------------
// AudioDisplacementNode (Main Thread GUI / Geometry Source)
// ---------------------------------------------------------------------------
AudioDisplacementNode::AudioDisplacementNode()
{
   mAudioSink = std::make_unique<AudioDisplaceAudioSink>();
   mAudioWaveform.assign(1024, 0.0f);
   mSmoothedSpectrum.assign(32, 0.0f);
}

AudioDisplacementNode::~AudioDisplacementNode() = default;

AudioNode* AudioDisplacementNode::GetAudioNode()
{
   if (!mAudioSink)
      mAudioSink = std::make_unique<AudioDisplaceAudioSink>();
   return mAudioSink.get();
}

int AudioDisplacementNode::ReadAudioScope(float* out, int capacity)
{
   // Single drain point for the ring is CookIfNeeded below - this reads the
   // persistent, fixed-length mAudioWaveform it maintains rather than
   // draining MeterRing a second time. Two independent readers draining the
   // same ring race for whichever samples arrived that frame (whichever ran
   // first got them, the other got nothing) - that was why mAudioWaveform's
   // length used to flap between 0 and 1024 depending on read order, which
   // in turn made the kModeNormalWaveform angle mapping below rescale
   // unpredictably every frame.
   const int count = std::min(capacity, (int)mAudioWaveform.size());
   if (count > 0 && out != nullptr)
      std::copy(mAudioWaveform.end() - count, mAudioWaveform.end(), out);
   return count;
}

void AudioDisplacementNode::CookIfNeeded(int frameId)
{
   if (mLastCookFrame == frameId)
      return;
   mLastCookFrame = frameId;

   if (auto* upstream = dynamic_cast<INode*>(input))
      upstream->CookIfNeeded(frameId);

   // Read live audio samples from ring buffer - the only drain point for it
   // (see ReadAudioScope's comment above). mAudioWaveform is kept at a fixed
   // length of 1024 as a rolling history rather than being re-`assign`ed to
   // whatever count happened to arrive this frame, which used to make its
   // length (and therefore every mode's angle/index mapping over it) flap
   // between 0 and 1024 samples frame to frame.
   float tempBuf[1024];
   const int readCount = mAudioSink ? mAudioSink->ReadSamples(tempBuf, 1024) : 0;

   if (readCount > 0)
   {
      if (mAudioWaveform.size() != 1024)
         mAudioWaveform.assign(1024, 0.0f);

      if (readCount >= 1024)
      {
         std::copy(tempBuf + (readCount - 1024), tempBuf + readCount, mAudioWaveform.begin());
      }
      else
      {
         std::move(mAudioWaveform.begin() + readCount, mAudioWaveform.end(), mAudioWaveform.begin());
         std::copy(tempBuf, tempBuf + readCount, mAudioWaveform.end() - readCount);
      }
      mAudioFrameCounter++;
   }

   // Compute RMS Energy
   float sumSq = 0.0f;
   for (float s : mAudioWaveform)
      sumSq += s * s;
   const float rawRms = sqrtf(sumSq / (float)std::max((size_t)1, mAudioWaveform.size()));
   const float targetEnergy = std::min(1.5f, rawRms * 2.5f);

   // Ballistics filter (Attack / Decay). dtSec used to be hard-coded to
   // 1/60s, which made the attack/decay ms params frame-rate dependent -
   // use the real elapsed time between CookIfNeeded calls instead.
   const auto now = std::chrono::steady_clock::now();
   float dtSec = 1.0f / 60.0f;
   if (mLastCookTime.time_since_epoch().count() != 0)
   {
      dtSec = (float)std::chrono::duration<double>(now - mLastCookTime).count();
      dtSec = std::clamp(dtSec, 1.0f / 1000.0f, 0.25f); // guard against a huge first-call/stall delta
   }
   mLastCookTime = now;

   const float tau = (targetEnergy > mCurrentEnergy) ? (attack * 0.001f) : (decay * 0.001f);
   const float alpha = (tau > 0.0001f) ? (1.0f - expf(-dtSec / tau)) : 1.0f;
   mCurrentEnergy += alpha * (targetEnergy - mCurrentEnergy);

   // Compute real-time FFT Spectrum across 32 frequency bands with dB normalization
   if (mAudioWaveform.size() >= 1024)
   {
      float re[1024];
      float im[1024];
      for (int i = 0; i < 1024; i++)
      {
         // Apply Hann window for crisp frequency isolation
         const float hann = 0.5f * (1.0f - cosf(6.283185307f * (float)i / 1023.0f));
         re[i] = mAudioWaveform[i] * hann;
         im[i] = 0.0f;
      }
      WaveTerrainDsp::Radix2FFT::Instance().Forward(re, im);

      constexpr int kNumBands = 32;
      for (int b = 0; b < kNumBands; b++)
      {
         const float fracLo = powf((float)b / (float)kNumBands, 2.0f);
         const float fracHi = powf((float)(b + 1) / (float)kNumBands, 2.0f);
         const int binLo = std::clamp((int)(fracLo * 512.0f), 1, 510);
         const int binHi = std::clamp((int)(fracHi * 512.0f) + 1, binLo + 1, 511);

         float bandMag = 0.0f;
         for (int k = binLo; k <= binHi; k++)
         {
            const float mag = sqrtf(re[k] * re[k] + im[k] * im[k]) / 128.0f;
            bandMag = std::max(bandMag, mag);
         }

         // Convert to perceptually scaled 0..1 range
         const float dbVal = 20.0f * log10f(std::max(1e-4f, bandMag));
         const float normVal = std::clamp((dbVal + 48.0f) / 48.0f, 0.0f, 1.2f);

         const float specAlpha = (normVal > mSmoothedSpectrum[b]) ? 0.45f : 0.15f;
         mSmoothedSpectrum[b] += specAlpha * (normVal - mSmoothedSpectrum[b]);
      }
   }

   mPhaseAccum += speed * 0.08f;
   if (mPhaseAccum > 6.283185307f * 1000.0f)
      mPhaseAccum = 0.0f;
}

AudioDisplacementNode::Signature AudioDisplacementNode::CurrentSignature() const
{
   Signature s;
   s.mode = mode;
   s.strength = strength;
   s.frequency = frequency;
   s.speed = speed;
   s.damping = damping;
   s.midlevel = midlevel;
   s.axis = axis;
   s.subdivide = subdivide;
   s.chladniM = chladniM;
   s.chladniN = chladniN;
   s.flat = flatShade;
   s.flip = flipNormals;
   s.selectionOnly = selectionOnly;
   s.upstream = input;
   s.upstreamRevision = input ? input->MeshRevision() : 0;
   s.energyQuant = (int)(mCurrentEnergy * 4096.0f);
   return s;
}

const Mesh& AudioDisplacementNode::GetMesh()
{
   if (input == nullptr)
      return kEmptyMesh;
   if (bypassed)
      return input->GetMesh();

   const Signature sig = CurrentSignature();
   if (mHasBuilt && sig == mBuilt)
      return mCache;

   const Mesh& rawSrc = input->GetMesh();
   if (rawSrc.vertices.empty())
      return kEmptyMesh;

   // Base mesh (subdivide + weld map + selection mask) only needs to rebuild
   // when the upstream mesh or the subdivide level actually changes - not
   // once per audio frame. This is the expensive part (Subdivide is up to
   // 256x the triangle count at level 4, plus a full weld-map pass).
   const bool baseCacheValid = mBaseHasCache && mBaseCachedUpstream == input &&
                              mBaseCachedUpstreamRevision == sig.upstreamRevision &&
                              mBaseCachedSubdivide == subdivide;
   if (!baseCacheValid)
   {
      mBaseMeshCache = (subdivide > 0)
         ? MeshOps::Subdivide(rawSrc, std::clamp(subdivide, 0, 4), 0.0f)
         : rawSrc;
      mWeldMapCache = MeshOps::BuildWeldMap(mBaseMeshCache);
      // Selection lives on rawSrc's faceMask; Subdivide doesn't propagate
      // faceMask to the subdivided mesh (there's no well-defined face
      // correspondence once triangles have been split), so the mask is
      // computed against rawSrc and mapped onto the subdivided mesh's
      // vertices by index - Subdivide keeps every original vertex at its
      // original index (repositioned in place) and only appends new
      // edge-point vertices after them, so indices < rawSrc.vertices.size()
      // carry over exactly. New edge-point vertices (introduced only when
      // subdivide > 0) have no single original vertex to inherit selection
      // from, since Subdivide doesn't expose which edge each descends from;
      // this treats them as selected (displaced normally) rather than
      // guessing - the boundary of a selection will bleed outward slightly
      // under subdivision as a result, which is a known limitation of
      // layering selectionOnly on top of a subdivide step, not a bug in the
      // mask logic itself.
      const std::vector<unsigned char> rawMask = MeshOps::VertexSelectionFromFaces(rawSrc);
      mVertexSelectionCache.assign(mBaseMeshCache.vertices.size(), 1);
      for (size_t i = 0; i < rawMask.size() && i < mVertexSelectionCache.size(); i++)
         mVertexSelectionCache[i] = rawMask[i];

      mBaseCachedUpstream = input;
      mBaseCachedUpstreamRevision = sig.upstreamRevision;
      mBaseCachedSubdivide = subdivide;
      mBaseHasCache = true;
   }

   const Mesh& src = mBaseMeshCache;
   const std::vector<unsigned int>& weldMap = mWeldMapCache;
   const bool applySelection = selectionOnly && !rawSrc.faceMask.empty();

   mCache = src;
   const size_t vCount = src.vertices.size();
   const float pi = 3.14159265358979323846f;

   std::vector<float> weldDisp(vCount, 0.0f);
   std::vector<float> weldDx(vCount, 0.0f);
   std::vector<float> weldDy(vCount, 0.0f);
   std::vector<float> weldDz(vCount, 0.0f);

   const bool hasWaveform = !mAudioWaveform.empty() && (mCurrentEnergy > 1e-4f);
   const int waveSize = (int)mAudioWaveform.size();
   const float energyScale = mCurrentEnergy * 4.0f;

   for (size_t i = 0; i < vCount; i++)
   {
      const size_t repr = (i < weldMap.size()) ? weldMap[i] : i;
      if (repr != i)
         continue;

      const Vertex& v = src.vertices[i];
      const float px = v.px;
      const float py = v.py;
      const float pz = v.pz;
      const float r = sqrtf(px * px + py * py + pz * pz);
      const float theta = atan2f(pz, px); // -pi .. pi
      const float phi = asinf(std::clamp(r > 1e-4f ? (py / r) : 0.0f, -1.0f, 1.0f));

      float disp = 0.0f;
      float dx = 0.0f, dy = 0.0f, dz = 0.0f;

      if (mCurrentEnergy > 1e-4f)
      {
         switch (mode)
         {
            case kModeNormalWaveform:
            {
               if (hasWaveform && waveSize > 0)
               {
                  const float angleFrac = (theta / (2.0f * pi) + 0.5f) * frequency + (py * 0.5f) - mPhaseAccum * 0.2f;
                  const float normU = angleFrac - floorf(angleFrac);
                  const float fIdx = normU * (float)(waveSize - 1);
                  const int i0 = (int)fIdx;
                  const int i1 = std::min(i0 + 1, waveSize - 1);
                  const float frac = fIdx - (float)i0;
                  const float sample = mAudioWaveform[i0] + (mAudioWaveform[i1] - mAudioWaveform[i0]) * frac;

                  const float standing = cosf(phi * frequency * 2.0f + mPhaseAccum);
                  disp = (sample * 2.0f + standing * 0.3f) * energyScale;
               }
               break;
            }
            case kModeSpectralBands:
            {
               if (!mSmoothedSpectrum.empty())
               {
                  const float angleNorm = (theta / (2.0f * pi) + 0.5f);
                  const float bandPos = angleNorm * (float)(mSmoothedSpectrum.size() - 1);
                  const int b0 = std::clamp((int)bandPos, 0, (int)mSmoothedSpectrum.size() - 1);
                  const int b1 = std::min(b0 + 1, (int)mSmoothedSpectrum.size() - 1);
                  const float frac = bandPos - (float)b0;
                  const float specVal = mSmoothedSpectrum[b0] + (mSmoothedSpectrum[b1] - mSmoothedSpectrum[b0]) * frac;

                  const float waveElev = sinf(py * frequency * 3.0f + mPhaseAccum * 2.0f);
                  disp = specVal * (1.0f + 0.4f * waveElev) * energyScale;
               }
               break;
            }
            case kModeAcousticRipples:
            {
               const float phase = r * frequency * 4.0f - mPhaseAccum * 4.0f;
               const float decayFactor = expf(-damping * r * 0.5f);
               const float wave = (sinf(phase) + 0.3f * sinf(phase * 2.0f)) * decayFactor;
               disp = wave * energyScale;
               break;
            }
            case kModeCymaticsChladni:
            {
               const float sx = px * frequency * pi;
               const float sy = py * frequency * pi;
               const float sz = pz * frequency * pi;
               const float nVal = (float)chladniN;
               const float mVal = (float)chladniM;

               const float psi1 = sinf(nVal * sx) * sinf(mVal * sy) - sinf(mVal * sx) * sinf(nVal * sy);
               const float psi2 = sinf(nVal * sy) * sinf(mVal * sz) - sinf(mVal * sy) * sinf(nVal * sz);
               const float osc = cosf(mPhaseAccum * 4.0f);
               const float pattern = (psi1 + psi2 * 0.7f) * osc;

               disp = pattern * energyScale * 0.6f;
               break;
            }
            case kModeDirectionalAxis:
            {
               if (hasWaveform && waveSize > 0)
               {
                  const float uPos = (axis == 0 ? py : (axis == 1 ? px : py)) * frequency - mPhaseAccum * 0.2f;
                  const float normU = uPos - floorf(uPos);
                  const int idx = std::clamp((int)(normU * (float)waveSize), 0, waveSize - 1);
                  const float val = (mAudioWaveform[idx] * 1.5f + sinf(uPos * 4.0f) * 0.3f) * energyScale * strength;
                  if (axis == 0) dx = val + midlevel * strength;
                  else if (axis == 1) dy = val + midlevel * strength;
                  else dz = val + midlevel * strength;
               }
               break;
            }
            default:
               break;
         }
      }

      if (mode != kModeDirectionalAxis)
         disp = (disp + midlevel) * strength;

      weldDisp[i] = disp;
      weldDx[i] = dx;
      weldDy[i] = dy;
      weldDz[i] = dz;
   }

   for (size_t i = 0; i < vCount; i++)
   {
      const Vertex& sv = src.vertices[i];
      Vertex& dv = mCache.vertices[i];

      if (applySelection && i < mVertexSelectionCache.size() && mVertexSelectionCache[i] == 0)
      {
         dv.px = sv.px;
         dv.py = sv.py;
         dv.pz = sv.pz;
         continue;
      }

      const size_t repr = (i < weldMap.size()) ? weldMap[i] : i;

      if (mode == kModeDirectionalAxis)
      {
         dv.px = sv.px + weldDx[repr];
         dv.py = sv.py + weldDy[repr];
         dv.pz = sv.pz + weldDz[repr];
      }
      else
      {
         const float disp = weldDisp[repr];
         dv.px = sv.px + sv.nx * disp;
         dv.py = sv.py + sv.ny * disp;
         dv.pz = sv.pz + sv.nz * disp;
      }
   }

   mCache = MeshOps::RecalculateNormals(mCache, flatShade, flipNormals);

   mBuilt = sig;
   mHasBuilt = true;
   mMeshRevision = NextMeshRevision();
   return mCache;
}

unsigned long long AudioDisplacementNode::MeshRevision()
{
   if (input == nullptr)
      return 0;
   if (bypassed)
      return input->MeshRevision();
   GetMesh();
   return mMeshRevision;
}

Material AudioDisplacementNode::GetMaterial() const
{
   if (inheritMaterial && input != nullptr)
      return input->GetMaterial();

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
   m.transmission = transmission;
   m.transmissionRoughness = transmissionRoughness;
   m.specular = specular;
   m.clearcoat = clearcoat;
   m.clearcoatRoughness = clearcoatRoughness;
   m.subsurface = subsurface;
   m.subsurfaceColor[0] = subsurfaceColor[0];
   m.subsurfaceColor[1] = subsurfaceColor[1];
   m.subsurfaceColor[2] = subsurfaceColor[2];
   m.subsurfaceRadius = subsurfaceRadius;
   return m;
}

unsigned int AudioDisplacementNode::GetSurfaceTexture()
{
   return input ? input->GetSurfaceTexture() : 0;
}

void AudioDisplacementNode::VisitParams(ParamVisitor& v)
{
   v.Int("mode", mode);
   v.Float("strength", strength);
   v.Float("frequency", frequency);
   v.Float("speed", speed);
   v.Float("damping", damping);
   v.Float("attack", attack);
   v.Float("decay", decay);
   v.Float("midlevel", midlevel);
   v.Int("axis", axis);
   v.Int("subdivide", subdivide);
   v.Int("chladniM", chladniM);
   v.Int("chladniN", chladniN);

   v.Bool("flat", flatShade);
   v.Bool("flip", flipNormals);
   v.Bool("inherit", inheritMaterial);
   v.Bool("selectionOnly", selectionOnly);

   v.Color("color", color);
   v.Float("metallic", metallic);
   v.Float("roughness", roughness);
   v.Float("opacity", opacity);
   v.Int("shading", shading);
   v.Color("emissionColor", emissionColor);
   v.Float("emission", emission);
   v.Float("ior", ior);
   v.Float("transmission", transmission);
   v.Float("transmissionRoughness", transmissionRoughness);
   v.Float("specular", specular);
   v.Float("clearcoat", clearcoat);
   v.Float("clearcoatRoughness", clearcoatRoughness);
   v.Float("subsurface", subsurface);
   v.Color("subsurfaceColor", subsurfaceColor);
   v.Float("subsurfaceRadius", subsurfaceRadius);
}
