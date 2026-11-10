// Music rig: turns audio-derived signals (beat phase, band energies) into scene parameter changes.
// Pure CPU, no allocation per frame, no audio-thread access: the caller passes a snapshot of the signals.
#pragma once
#include "engine/Scene.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace engine
{
   struct MusicSignals
   {
      float beatPhase = 0.0f; // 0..1 within the beat
      float beatPulse = 0.0f; // 1 on the beat, decays
      float low = 0.0f, mid = 0.0f, high = 0.0f; // 0..1 smoothed band energies
   };

   enum class RigSource { BeatPulse, BeatSine, Low, Mid, High };
   enum class RigTarget { ScaleY, ScaleUniform, OffsetY, LightIntensity, Exposure };

   struct RigBinding
   {
      RigSource source = RigSource::BeatPulse;
      RigTarget target = RigTarget::ScaleUniform;
      int index = 0;       // instance or light index (ignored for Exposure)
      float amount = 0.2f; // how far the target moves at source = 1
      float attack = 0.6f, release = 0.15f; // per-frame smoothing toward the source (0..1)
   };

   class MusicRig
   {
   public:
      void SetBindings(std::vector<RigBinding> b) { mB = std::move(b); mState.assign(mB.size(), 0.0f); mBase.clear(); }
      // Captures rest values on the first call so repeated Apply never accumulates.
      void Apply(Frame& f, const MusicSignals& s)
      {
         if (mBase.empty() && !f.instances.empty()) { mBase.reserve(f.instances.size()); for (const Instance& i : f.instances) mBase.push_back(i); }
         if (mBaseLightI.empty()) { for (const Light& l : f.lights) mBaseLightI.push_back(l.intensity); mBaseExposure = f.exposure; }
         // reset to rest
         for (size_t i = 0; i < f.instances.size() && i < mBase.size(); i++) f.instances[i] = mBase[i];
         for (size_t i = 0; i < f.lights.size() && i < mBaseLightI.size(); i++) f.lights[i].intensity = mBaseLightI[i];
         f.exposure = mBaseExposure;
         for (size_t k = 0; k < mB.size(); k++)
         {
            const RigBinding& b = mB[k];
            float v = 0.0f;
            switch (b.source)
            {
            case RigSource::BeatPulse: v = s.beatPulse; break;
            case RigSource::BeatSine: v = 0.5f - 0.5f * std::cos(6.2831853f * s.beatPhase); break;
            case RigSource::Low: v = s.low; break;
            case RigSource::Mid: v = s.mid; break;
            case RigSource::High: v = s.high; break;
            }
            float& st = mState[k];
            st += (v - st) * (v > st ? b.attack : b.release);
            const float d = st * b.amount;
            switch (b.target)
            {
            case RigTarget::ScaleY: ScaleAxis(f, b.index, 1, 1.0f + d); break;
            case RigTarget::ScaleUniform: for (int a = 0; a < 3; a++) ScaleAxis(f, b.index, a, 1.0f + d); break;
            case RigTarget::OffsetY: if (b.index >= 0 && b.index < (int)f.instances.size()) f.instances[b.index].model[13] += d; break;
            case RigTarget::LightIntensity: if (b.index >= 0 && b.index < (int)f.lights.size()) f.lights[b.index].intensity *= 1.0f + d; break;
            case RigTarget::Exposure: f.exposure *= 1.0f + d; break;
            }
         }
      }
   private:
      static void ScaleAxis(Frame& f, int idx, int axis, float k)
      {
         if (idx < 0 || idx >= (int)f.instances.size()) return;
         Instance& in = f.instances[idx];
         for (int r = 0; r < 3; r++) in.model[axis * 4 + r] *= k; // column `axis` of the column-major model matrix
         // normal matrix: inverse-transpose scales that axis by 1/k; keep normals unit-ish
         for (int r = 0; r < 3; r++) in.normalMatrix[axis * 4 + r] /= k;
      }
      std::vector<RigBinding> mB;
      std::vector<float> mState;
      std::vector<Instance> mBase;
      std::vector<float> mBaseLightI;
      float mBaseExposure = 1.0f;
   };
}
