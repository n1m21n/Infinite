#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>

#include "audio/DspMath.h"

// Physical Modelling Metallic Synthesizer DSP Kernel.
// Combines:
//  1. Mallet Strike Exciter with transient pulse shaping and noise burst
//  2. Bank of 12 Inharmonic Second-Order Modal Resonators per voice
//  3. Dispersive Delay-Line Waveguide (Extended Karplus-Strong with Allpass Dispersion)
//  4. Material Physics profiles (Steel, Bell, Gong, Iron, Titanium, Glass, Ceramic, Vibraphone)
//  5. Master SVF Filter (LP/HP/BP) + Soft Drive + Stereo Modal Spatialization
namespace MetallicDsp
{
   enum MaterialPreset
   {
      kSteel = 0,
      kBell,
      kGong,
      kIron,
      kTitanium,
      kGlass,
      kCeramic,
      kVibraphone,
      kNumMaterials
   };

   inline const char* const* MaterialNames()
   {
      static const char* const kNames[kNumMaterials] = {
         "Steel",
         "Bell Bronze",
         "Gong / Plate",
         "Cast Iron",
         "Titanium",
         "Glass / Crystal",
         "Ceramic",
         "Vibraphone Bar"
      };
      return kNames;
   }

   inline const char* MaterialName(int mat)
   {
      return (mat >= 0 && mat < kNumMaterials) ? MaterialNames()[mat] : MaterialNames()[kSteel];
   }

   inline const std::vector<std::string>& MaterialList()
   {
      static std::vector<std::string> list;
      if (list.empty())
      {
         for (int i = 0; i < kNumMaterials; i++)
            list.push_back(MaterialNames()[i]);
      }
      return list;
   }

   enum FilterMode
   {
      kFilterLP12 = 0,
      kFilterLP24,
      kFilterHP12,
      kFilterBP,
      kFilterOff,
      kNumFilterModes
   };

   inline const char* const* FilterModeNames()
   {
      static const char* const kNames[kNumFilterModes] = {
         "Lowpass 12",
         "Lowpass 24",
         "Highpass 12",
         "Bandpass",
         "Bypass"
      };
      return kNames;
   }

   inline const std::vector<std::string>& FilterModeList()
   {
      static std::vector<std::string> list;
      if (list.empty())
      {
         for (int i = 0; i < kNumFilterModes; i++)
            list.push_back(FilterModeNames()[i]);
      }
      return list;
   }

   constexpr int kNumModes = 12;
   constexpr int kMaxDelaySamples = 4096;

   // Physical properties for each material preset
   struct MaterialProfile
   {
      float defaultStiffness;       // 0..1 inharmonicity base
      float defaultDecay;           // seconds
      float highFreqLoss;           // damping rate of high partials (0.1 = singing, 1.5 = dry)
      float dispersionCoeff;        // allpass dispersion strength
      float modeRatios[kNumModes];  // inharmonic mode frequency ratios
      float modeAmplitudes[kNumModes]; // mode excitation gains
      float strikeHardness;         // transient brightness multiplier
      float nonLinearBeating;       // frequency detune between mode pairs
   };

   inline MaterialProfile GetMaterialProfile(int mat)
   {
      MaterialProfile p = {};
      switch (mat)
      {
      case kSteel:
         p.defaultStiffness = 0.25f;
         p.defaultDecay = 2.2f;
         p.highFreqLoss = 0.35f;
         p.dispersionCoeff = 0.30f;
         p.strikeHardness = 1.2f;
         p.nonLinearBeating = 0.002f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 1.00f;
         p.modeRatios[1] = 2.756f; p.modeAmplitudes[1] = 0.75f;
         p.modeRatios[2] = 5.404f; p.modeAmplitudes[2] = 0.55f;
         p.modeRatios[3] = 8.933f; p.modeAmplitudes[3] = 0.40f;
         p.modeRatios[4] = 13.344f; p.modeAmplitudes[4] = 0.28f;
         p.modeRatios[5] = 18.640f; p.modeAmplitudes[5] = 0.20f;
         p.modeRatios[6] = 24.815f; p.modeAmplitudes[6] = 0.14f;
         p.modeRatios[7] = 31.870f; p.modeAmplitudes[7] = 0.10f;
         p.modeRatios[8] = 39.800f; p.modeAmplitudes[8] = 0.07f;
         p.modeRatios[9] = 48.600f; p.modeAmplitudes[9] = 0.05f;
         p.modeRatios[10] = 58.200f; p.modeAmplitudes[10] = 0.03f;
         p.modeRatios[11] = 68.700f; p.modeAmplitudes[11] = 0.02f;
         break;

      case kBell:
         p.defaultStiffness = 0.75f;
         p.defaultDecay = 3.5f;
         p.highFreqLoss = 0.20f;
         p.dispersionCoeff = 0.55f;
         p.strikeHardness = 1.4f;
         p.nonLinearBeating = 0.005f;
         p.modeRatios[0] = 0.500f; p.modeAmplitudes[0] = 0.65f;
         p.modeRatios[1] = 1.000f; p.modeAmplitudes[1] = 1.00f;
         p.modeRatios[2] = 1.189f; p.modeAmplitudes[2] = 0.85f;
         p.modeRatios[3] = 1.500f; p.modeAmplitudes[3] = 0.70f;
         p.modeRatios[4] = 2.000f; p.modeAmplitudes[4] = 0.60f;
         p.modeRatios[5] = 2.742f; p.modeAmplitudes[5] = 0.45f;
         p.modeRatios[6] = 3.820f; p.modeAmplitudes[6] = 0.35f;
         p.modeRatios[7] = 5.120f; p.modeAmplitudes[7] = 0.25f;
         p.modeRatios[8] = 6.700f; p.modeAmplitudes[8] = 0.18f;
         p.modeRatios[9] = 8.520f; p.modeAmplitudes[9] = 0.12f;
         p.modeRatios[10] = 10.60f; p.modeAmplitudes[10] = 0.08f;
         p.modeRatios[11] = 13.00f; p.modeAmplitudes[11] = 0.05f;
         break;

      case kGong:
         p.defaultStiffness = 0.90f;
         p.defaultDecay = 4.0f;
         p.highFreqLoss = 0.28f;
         p.dispersionCoeff = 0.70f;
         p.strikeHardness = 0.9f;
         p.nonLinearBeating = 0.012f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 0.90f;
         p.modeRatios[1] = 1.482f; p.modeAmplitudes[1] = 0.85f;
         p.modeRatios[2] = 2.045f; p.modeAmplitudes[2] = 0.75f;
         p.modeRatios[3] = 2.684f; p.modeAmplitudes[3] = 0.65f;
         p.modeRatios[4] = 3.420f; p.modeAmplitudes[4] = 0.55f;
         p.modeRatios[5] = 4.250f; p.modeAmplitudes[5] = 0.45f;
         p.modeRatios[6] = 5.180f; p.modeAmplitudes[6] = 0.38f;
         p.modeRatios[7] = 6.300f; p.modeAmplitudes[7] = 0.30f;
         p.modeRatios[8] = 7.820f; p.modeAmplitudes[8] = 0.22f;
         p.modeRatios[9] = 9.450f; p.modeAmplitudes[9] = 0.16f;
         p.modeRatios[10] = 11.20f; p.modeAmplitudes[10] = 0.10f;
         p.modeRatios[11] = 13.50f; p.modeAmplitudes[11] = 0.06f;
         break;

      case kIron:
         p.defaultStiffness = 0.40f;
         p.defaultDecay = 1.2f;
         p.highFreqLoss = 0.85f;
         p.dispersionCoeff = 0.45f;
         p.strikeHardness = 1.3f;
         p.nonLinearBeating = 0.003f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 1.00f;
         p.modeRatios[1] = 2.210f; p.modeAmplitudes[1] = 0.70f;
         p.modeRatios[2] = 3.840f; p.modeAmplitudes[2] = 0.45f;
         p.modeRatios[3] = 5.720f; p.modeAmplitudes[3] = 0.30f;
         p.modeRatios[4] = 8.100f; p.modeAmplitudes[4] = 0.18f;
         p.modeRatios[5] = 11.200f; p.modeAmplitudes[5] = 0.10f;
         p.modeRatios[6] = 14.800f; p.modeAmplitudes[6] = 0.06f;
         p.modeRatios[7] = 19.100f; p.modeAmplitudes[7] = 0.03f;
         p.modeRatios[8] = 24.200f; p.modeAmplitudes[8] = 0.02f;
         p.modeRatios[9] = 30.100f; p.modeAmplitudes[9] = 0.01f;
         p.modeRatios[10] = 36.800f; p.modeAmplitudes[10] = 0.005f;
         p.modeRatios[11] = 44.500f; p.modeAmplitudes[11] = 0.002f;
         break;

      case kTitanium:
         p.defaultStiffness = 0.60f;
         p.defaultDecay = 2.8f;
         p.highFreqLoss = 0.18f;
         p.dispersionCoeff = 0.65f;
         p.strikeHardness = 1.6f;
         p.nonLinearBeating = 0.004f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 0.95f;
         p.modeRatios[1] = 2.820f; p.modeAmplitudes[1] = 0.80f;
         p.modeRatios[2] = 5.560f; p.modeAmplitudes[2] = 0.65f;
         p.modeRatios[3] = 9.180f; p.modeAmplitudes[3] = 0.50f;
         p.modeRatios[4] = 13.700f; p.modeAmplitudes[4] = 0.38f;
         p.modeRatios[5] = 19.200f; p.modeAmplitudes[5] = 0.28f;
         p.modeRatios[6] = 25.600f; p.modeAmplitudes[6] = 0.20f;
         p.modeRatios[7] = 33.000f; p.modeAmplitudes[7] = 0.14f;
         p.modeRatios[8] = 41.500f; p.modeAmplitudes[8] = 0.09f;
         p.modeRatios[9] = 51.200f; p.modeAmplitudes[9] = 0.06f;
         p.modeRatios[10] = 62.000f; p.modeAmplitudes[10] = 0.04f;
         p.modeRatios[11] = 74.000f; p.modeAmplitudes[11] = 0.02f;
         break;

      case kGlass:
         p.defaultStiffness = 0.20f;
         p.defaultDecay = 3.2f;
         p.highFreqLoss = 0.12f;
         p.dispersionCoeff = 0.25f;
         p.strikeHardness = 1.8f;
         p.nonLinearBeating = 0.001f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 1.00f;
         p.modeRatios[1] = 2.320f; p.modeAmplitudes[1] = 0.85f;
         p.modeRatios[2] = 4.150f; p.modeAmplitudes[2] = 0.70f;
         p.modeRatios[3] = 6.450f; p.modeAmplitudes[3] = 0.55f;
         p.modeRatios[4] = 9.220f; p.modeAmplitudes[4] = 0.40f;
         p.modeRatios[5] = 12.400f; p.modeAmplitudes[5] = 0.28f;
         p.modeRatios[6] = 16.100f; p.modeAmplitudes[6] = 0.18f;
         p.modeRatios[7] = 20.200f; p.modeAmplitudes[7] = 0.10f;
         p.modeRatios[8] = 24.800f; p.modeAmplitudes[8] = 0.06f;
         p.modeRatios[9] = 30.000f; p.modeAmplitudes[9] = 0.03f;
         p.modeRatios[10] = 35.800f; p.modeAmplitudes[10] = 0.015f;
         p.modeRatios[11] = 42.200f; p.modeAmplitudes[11] = 0.008f;
         break;

      case kCeramic:
         p.defaultStiffness = 0.80f;
         p.defaultDecay = 0.6f;
         p.highFreqLoss = 1.20f;
         p.dispersionCoeff = 0.50f;
         p.strikeHardness = 1.5f;
         p.nonLinearBeating = 0.008f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 1.00f;
         p.modeRatios[1] = 1.850f; p.modeAmplitudes[1] = 0.60f;
         p.modeRatios[2] = 3.120f; p.modeAmplitudes[2] = 0.35f;
         p.modeRatios[3] = 4.750f; p.modeAmplitudes[3] = 0.20f;
         p.modeRatios[4] = 6.800f; p.modeAmplitudes[4] = 0.10f;
         p.modeRatios[5] = 9.300f; p.modeAmplitudes[5] = 0.05f;
         p.modeRatios[6] = 12.200f; p.modeAmplitudes[6] = 0.02f;
         p.modeRatios[7] = 15.600f; p.modeAmplitudes[7] = 0.01f;
         p.modeRatios[8] = 19.500f; p.modeAmplitudes[8] = 0.005f;
         p.modeRatios[9] = 24.000f; p.modeAmplitudes[9] = 0.002f;
         p.modeRatios[10] = 29.200f; p.modeAmplitudes[10] = 0.001f;
         p.modeRatios[11] = 35.000f; p.modeAmplitudes[11] = 0.0005f;
         break;

      case kVibraphone:
      default:
         p.defaultStiffness = 0.10f;
         p.defaultDecay = 3.0f;
         p.highFreqLoss = 0.25f;
         p.dispersionCoeff = 0.15f;
         p.strikeHardness = 1.0f;
         p.nonLinearBeating = 0.0015f;
         p.modeRatios[0] = 1.000f; p.modeAmplitudes[0] = 1.00f;
         p.modeRatios[1] = 3.980f; p.modeAmplitudes[1] = 0.55f;
         p.modeRatios[2] = 9.850f; p.modeAmplitudes[2] = 0.30f;
         p.modeRatios[3] = 17.500f; p.modeAmplitudes[3] = 0.18f;
         p.modeRatios[4] = 27.200f; p.modeAmplitudes[4] = 0.10f;
         p.modeRatios[5] = 38.800f; p.modeAmplitudes[5] = 0.05f;
         p.modeRatios[6] = 52.400f; p.modeAmplitudes[6] = 0.025f;
         p.modeRatios[7] = 68.000f; p.modeAmplitudes[7] = 0.012f;
         p.modeRatios[8] = 85.500f; p.modeAmplitudes[8] = 0.006f;
         p.modeRatios[9] = 105.00f; p.modeAmplitudes[9] = 0.003f;
         p.modeRatios[10] = 126.50f; p.modeAmplitudes[10] = 0.001f;
         p.modeRatios[11] = 150.00f; p.modeAmplitudes[11] = 0.0005f;
         break;
      }
      return p;
   }

   // -------------------------------------------------------------------
   // 2nd-Order Resonant Bandpass Mode (Direct Form II / Biquad Resonator)
   // -------------------------------------------------------------------
   struct ResonantMode
   {
      float s1 = 0.0f;
      float s2 = 0.0f;
      float a1 = 0.0f;
      float a2 = 0.0f;
      float gain = 0.0f;
      float panL = 0.707f;
      float panR = 0.707f;

      // Setup() writes the *target* coefficients; Process() slews the live ones
      // toward them. A pole this close to the unit circle can't have its centre
      // frequency stepped with state preserved - the discontinuity train that
      // produces is the buzz. SnapToTargets() is for note-on, where a jump is
      // correct because the state is being cleared anyway.
      float targetA1 = 0.0f;
      float targetA2 = 0.0f;
      float targetGain = 0.0f;
      float targetPanL = 0.707f;
      float targetPanR = 0.707f;

      void Reset()
      {
         s1 = 0.0f;
         s2 = 0.0f;
      }

      void Setup(float freqHz, float t60Sec, float amplitude, float pan, double sampleRate)
      {
         if (sampleRate <= 0.0) return;
         // Modes above 0.45 * SR are muted rather than clamped to Nyquist. Several
         // materials put most of their 12 ratios out of band at normal fundamentals
         // (kVibraphone ratio 150, kTitanium 74); clamping stacks them all at the
         // top of the spectrum with r ~= 1, which is a shriek by construction.
         const float maxHz = (float)sampleRate * 0.45f;
         const bool inBand = (freqHz <= maxHz);
         const float f = std::clamp(freqHz, 10.0f, maxHz);
         const float dt = 1.0f / (float)sampleRate;

         // The pole radius ceiling used to be a fixed 0.99995, which is a T60
         // cap that scales inversely with sample rate (~3.1s at 44.1kHz,
         // ~1.4s at 96kHz) - the mode bank physically could not ring past
         // that no matter what the decay knob asked for. Derive the ceiling
         // from a fixed max T60 instead so it holds across sample rates, with
         // a hard sub-1.0 guard so the Direct Form II biquad (a2 = -r*r)
         // never sees an undamped pole.
         const float rMax = std::min(0.999999f, expf(-6.907755f * dt / 40.0f));
         const float r = std::clamp(expf(-6.907755f * dt / std::max(0.005f, t60Sec)), 0.0f, rMax);
         const float w = 2.0f * (float)M_PI * f * dt;

         targetA1 = 2.0f * r * cosf(w);
         targetA2 = -r * r;
         targetGain = inBand ? (1.0f - r) * amplitude : 0.0f;

         DspMath::EqualPowerPan(pan, targetPanL, targetPanR);
      }

      void SnapToTargets()
      {
         a1 = targetA1;
         a2 = targetA2;
         gain = targetGain;
         panL = targetPanL;
         panR = targetPanR;
      }

      inline void Process(float in, float slew, float& outL, float& outR)
      {
         a1 += (targetA1 - a1) * slew;
         a2 += (targetA2 - a2) * slew;
         gain += (targetGain - gain) * slew;
         panL += (targetPanL - panL) * slew;
         panR += (targetPanR - panR) * slew;

         const float y = gain * in + a1 * s1 + a2 * s2;
         s2 = s1;
         s1 = y;
         outL += y * panL;
         outR += y * panR;
      }
   };

   // -------------------------------------------------------------------
   // First-order Allpass Filter for Dispersive Waveguide Inharmonicity
   // -------------------------------------------------------------------
   struct AllpassStage
   {
      float c = 0.0f;
      float s = 0.0f;

      void Reset() { s = 0.0f; }

      inline float Process(float in)
      {
         const float y = c * in + s;
         s = in - c * y;
         return y;
      }
   };

   // -------------------------------------------------------------------
   // Mallet Strike Exciter Generator
   // -------------------------------------------------------------------
   struct MalletExciter
   {
      DspMath::WhiteNoise noise;
      DspMath::TptSvf filter;
      float env = 0.0f;
      float envDecay = 0.0f;
      float pulsePhase = 0.0f;
      float pulseInc = 0.0f;
      float hardness = 1.0f;
      float noiseMix = 0.2f;
      bool active = false;

      void Reset()
      {
         env = 0.0f;
         pulsePhase = 0.0f;
         active = false;
         filter.Reset();
      }

      void Trigger(float velocity, float transientHardness, float fundamentalHz, double sampleRate)
      {
         if (sampleRate <= 0.0) return;
         hardness = std::clamp(transientHardness, 0.05f, 2.0f);
         const float durationMs = (14.0f - hardness * 6.0f) * (1.05f - 0.5f * velocity);
         const float decaySamples = durationMs * 0.001f * (float)sampleRate;
         envDecay = expf(-1.0f / std::max(2.0f, decaySamples));
         env = std::clamp(velocity, 0.05f, 1.0f);

         const float pulseFreq = std::clamp(fundamentalHz * (1.5f + hardness * 3.5f), 100.0f, 12000.0f);
         pulseInc = pulseFreq / (float)sampleRate;
         pulsePhase = 0.0f;

         const float cutoff = std::clamp(fundamentalHz * (2.0f + hardness * 8.0f), 200.0f, (float)sampleRate * 0.48f);
         filter.SetSampleRate(sampleRate);
         filter.SetCutoff(cutoff, 0.707f);
         noiseMix = std::clamp(0.15f + 0.35f * hardness, 0.05f, 0.8f);

         active = true;
      }

      inline float Process()
      {
         if (!active || env < 0.0001f)
         {
            active = false;
            return 0.0f;
         }

         float pulse = 0.0f;
         if (pulsePhase < 1.0f)
         {
            pulse = sinf((float)M_PI * pulsePhase);
            pulsePhase += pulseInc;
         }

         float n = noise.Next();
         auto filtOut = filter.Process(n);
         float exciter = pulse * (1.0f - noiseMix) + filtOut.low * noiseMix;

         float out = exciter * env;
         env *= envDecay;
         return out;
      }
   };

   // -------------------------------------------------------------------
   // Single Polyphonic Metallic Voice
   // -------------------------------------------------------------------
   struct MetallicVoice
   {
      ResonantMode modes[kNumModes];
      MalletExciter exciter;

      float delayBuffer[kMaxDelaySamples] = {};
      int writePos = 0;
      float delayLength = 100.0f;
      float loopGain = 0.98f;
      DspMath::OnePole loopFilter;
      AllpassStage dispersionChain[4];

      int midiNote = -1;
      int voiceId = 0;
      float velocity = 0.0f;
      float currentFreq = 220.0f;
      uint64_t age = 0;
      bool active = false;
      float ampDecay = 0.0f;
      float voiceLevel = 0.0f;
      // Set by Release(); once true, UpdateAcoustics() stops recomputing
      // ampDecay from the decay knob so a control-rate refresh can't slow the
      // envelope back down after note-off already sped it up.
      bool released = false;
      // Consecutive samples the post-envelope output has stayed below
      // audibility. Frees the voice on real silence rather than waiting on
      // the slow safety-net envelope - see ampDecay's comment in Trigger().
      int silentSamples = 0;

      // Per-voice portamento. Shared glide state would drag every held note
      // toward one pitch; seeding it at Trigger() means a new note starts *at*
      // pitch instead of sweeping up from 0 Hz.
      DspMath::OnePole pitchSmoother;

      // ~5 ms coefficient slew, recomputed with the sample rate.
      float coefSlew = 0.02f;

      // Last inputs UpdateAcoustics() was called with, so a settled patch is
      // completely static instead of rewriting 12 biquads every control block.
      int cachedMaterial = -1;
      float cachedFreq = -1.0f;
      float cachedDecay = -1.0f;
      float cachedStiffness = -1.0f;
      float cachedSpread = -2.0f;

      // Cached so Process() (which takes no sampleRate) can size the
      // silence-window used to free a voice - see silentSamples above.
      double voiceSampleRate = 44100.0;

      void Reset()
      {
         for (int i = 0; i < kNumModes; i++)
            modes[i].Reset();
         exciter.Reset();
         for (int i = 0; i < kMaxDelaySamples; i++)
            delayBuffer[i] = 0.0f;
         writePos = 0;
         for (int i = 0; i < 4; i++)
            dispersionChain[i].Reset();
         loopFilter.SetImmediate(0.0f);
         active = false;
         midiNote = -1;
         voiceLevel = 0.0f;
         released = false;
         silentSamples = 0;
         cachedMaterial = -1;
         cachedFreq = -1.0f;
         cachedDecay = -1.0f;
         cachedStiffness = -1.0f;
         cachedSpread = -2.0f;
      }

      void Trigger(int note, float vel, int id, float freqHz, float transient, float decaySec,
                   float stiffness, int materialPreset, float stereoSpread, double sampleRate)
      {
         // A stolen voice still holds 4096 samples of the previous note's
         // waveguide energy; re-reading that at a shorter delay length with
         // loopGain near 1 is a pitched-up feedback squeal. Always start clean.
         Reset();

         midiNote = note;
         velocity = vel;
         voiceId = id;
         currentFreq = freqHz;
         active = true;
         voiceLevel = 1.0f;
         coefSlew = (sampleRate > 0.0) ? (1.0f - expf(-1.0f / (0.005f * (float)sampleRate))) : 0.02f;
         pitchSmoother.SetImmediate(freqHz);

         const MaterialProfile mat = GetMaterialProfile(materialPreset);
         const float effectiveHardness = std::clamp(transient * mat.strikeHardness, 0.1f, 2.5f);

         exciter.Trigger(vel, effectiveHardness, freqHz, sampleRate);
         UpdateAcoustics(freqHz, decaySec, stiffness, materialPreset, stereoSpread, sampleRate, true);
         ampDecay = ComputeAmpDecay(decaySec, sampleRate);
      }

      // The `decay` knob is documented in seconds and is what UpdateAcoustics
      // hands to each mode's own T60 pole (ResonantMode::Setup), so the modal
      // decay alone already matches the knob. ampDecay used to add a second,
      // independent -80 dB/1.5x-decay envelope on top, and the two rates
      // summed - a 9.9s knob produced ~5.2s of audible ring even with the
      // pole ceiling in Setup() raised. Pre-compensating exactly would mean
      // ampDecay contributes zero loss, at which point it can't do its job
      // (killing a voice that never otherwise reaches the silence floor, e.g.
      // if a future mode/material combination decays slower than expected).
      // Instead make it a safety net far below the modal rate - only ~8 dB of
      // its 80 dB budget has elapsed by the time the modal T60 hits, so the
      // combined T60 stays within the fixture's tolerance - and free the
      // voice on measured silence (see silentSamples in Process()) rather
      // than waiting on this envelope's own -80 dB point.
      static float ComputeAmpDecay(float decaySec, double sampleRate)
      {
         const float effectiveDecay = std::clamp(decaySec, 0.02f, 20.0f);
         const float totalDecaySamples = effectiveDecay * 8.0f * (float)sampleRate;
         return expf(-9.2103f / std::max(64.0f, totalDecaySamples));
      }

      // Advances the per-voice glide by one control block of `blockSamples` and
      // returns the pitch to retune to.
      float GlideTo(float targetHz, float glideSec, int blockSamples, double sampleRate)
      {
         if (glideSec <= 0.001f || sampleRate <= 0.0 || blockSamples <= 0)
         {
            pitchSmoother.SetImmediate(targetHz);
            return targetHz;
         }
         pitchSmoother.coeff = expf(-(float)blockSamples / (glideSec * (float)sampleRate));
         return pitchSmoother.Process(targetHz);
      }

      void UpdateAcoustics(float freqHz, float decaySec, float stiffness, int materialPreset,
                           float stereoSpread, double sampleRate, bool immediate = false)
      {
         if (!immediate &&
             materialPreset == cachedMaterial &&
             std::fabs(freqHz - cachedFreq) < 0.01f &&
             std::fabs(decaySec - cachedDecay) < 0.0005f &&
             std::fabs(stiffness - cachedStiffness) < 0.0005f &&
             std::fabs(stereoSpread - cachedSpread) < 0.0005f)
         {
            return;
         }
         cachedMaterial = materialPreset;
         cachedFreq = freqHz;
         cachedDecay = decaySec;
         cachedStiffness = stiffness;
         cachedSpread = stereoSpread;

         currentFreq = freqHz;
         voiceSampleRate = sampleRate;
         const MaterialProfile mat = GetMaterialProfile(materialPreset);
         const float effectiveStiffness = std::clamp(stiffness * mat.defaultStiffness * 2.0f, 0.0f, 2.5f);
         // The knob is documented in seconds ("%.2f s" in the UI) and
         // SetMaterialPreset() already seeds `decay` from the material's
         // default on preset change, so effectiveDecay is the knob value
         // itself - not further rescaled per material. It used to be
         // multiplied by (mat.defaultDecay / 2), so the same "9.00 s" reading
         // meant 2.7s on Ceramic and 18s on Gong.
         const float effectiveDecay = std::clamp(decaySec, 0.02f, 20.0f);

         for (int m = 0; m < kNumModes; m++)
         {
            const float inharm = sqrtf(1.0f + effectiveStiffness * (float)(m * m));
            const float beat = 1.0f + (m % 2 == 1 ? mat.nonLinearBeating : -mat.nonLinearBeating) * (float)m;
            const float modeFreq = freqHz * mat.modeRatios[m] * inharm * beat;

            const float modeLoss = expf(-mat.highFreqLoss * (float)m * 0.4f);
            const float modeDecay = effectiveDecay * modeLoss;
            const float amp = mat.modeAmplitudes[m] * (velocity > 0.0f ? velocity : 0.8f);
            const float modePan = (m == 0) ? 0.0f : ((m % 2 == 1 ? 1.0f : -1.0f) * stereoSpread * (0.3f + 0.7f * ((float)m / (float)kNumModes)));

            modes[m].Setup(modeFreq, modeDecay, amp, modePan, sampleRate);
            if (immediate)
               modes[m].SnapToTargets();
         }

         const float samplesPerPeriod = (sampleRate > 0.0) ? (float)sampleRate / std::max(20.0f, freqHz) : 100.0f;
         delayLength = std::clamp(samplesPerPeriod, 8.0f, (float)(kMaxDelaySamples - 1));

         const float loopLoss = expf(-6.907755f * (delayLength / (float)sampleRate) / effectiveDecay);
         loopGain = std::clamp(loopLoss, 0.0f, 0.999f);

         loopFilter.SetTimeConstant(0.00005f * (1.0f + mat.highFreqLoss * 2.0f), sampleRate);

         const float allpassC = std::clamp(mat.dispersionCoeff * effectiveStiffness * 0.7f, 0.0f, 0.85f);
         for (int i = 0; i < 4; i++)
         {
            dispersionChain[i].c = allpassC;
         }

         // Re-derive ampDecay from the (possibly just-changed) decay knob so
         // moving it while a voice rings updates the safety-net envelope too,
         // not just the modal decay set up above. Skipped once Release() has
         // fired: that already sped ampDecay up for note-off, and the next
         // control block recomputing it from the still-held knob would slow
         // it back down and undo the release.
         if (!released)
            ampDecay = ComputeAmpDecay(decaySec, sampleRate);
      }

      // A struck metal bar keeps ringing after the mallet lifts - its modal
      // decay (set up from the `decay` knob) doesn't change on note-off, so
      // release only needs to retarget the same safety-net envelope
      // ComputeAmpDecay() already uses, at `releaseSec` in place of the live
      // decay knob. Passing releaseSec == effectiveDecay (the caller's
      // default) reproduces the un-released envelope almost exactly, i.e.
      // "don't damp beyond the knob's own decay"; a smaller releaseSec damps
      // harder. `std::min` only ever speeds ampDecay up here, never slows it
      // back down, matching the "once released, stay released" contract
      // UpdateAcoustics() relies on via the `released` flag.
      void Release(double sampleRate, float releaseSec = 0.4f)
      {
         released = true;
         ampDecay = std::min(ampDecay, ComputeAmpDecay(releaseSec, sampleRate));
      }

      inline void Process(float& outL, float& outR)
      {
         if (!active) return;

         const float strike = exciter.Process();

         float modalL = 0.0f;
         float modalR = 0.0f;
         for (int m = 0; m < kNumModes; m++)
         {
            modes[m].Process(strike, coefSlew, modalL, modalR);
         }

         float readPos = (float)writePos - delayLength;
         if (readPos < 0.0f) readPos += (float)kMaxDelaySamples;
         const int r0 = (int)readPos;
         const int r1 = (r0 + 1) % kMaxDelaySamples;
         const float frac = readPos - (float)r0;
         float delayed = delayBuffer[r0] + frac * (delayBuffer[r1] - delayBuffer[r0]);

         for (int i = 0; i < 4; i++)
         {
            delayed = dispersionChain[i].Process(delayed);
         }

         delayed = loopFilter.Process(delayed);

         float loopSignal = strike * 0.8f + delayed * loopGain;
         if (std::fabs(loopSignal) < 1e-20f)
            loopSignal = 0.0f;
         delayBuffer[writePos] = loopSignal;
         writePos = (writePos + 1) % kMaxDelaySamples;

         const float wgSample = delayed * 0.4f;
         const float vL = (modalL * 0.7f + wgSample) * voiceLevel;
         const float vR = (modalR * 0.7f + wgSample) * voiceLevel;

         // Two nested feedback structures with runtime-modulated coefficients
         // will eventually produce a non-finite sample; kill the voice instead
         // of letting NaN propagate into the mix bus.
         if (!std::isfinite(vL) || !std::isfinite(vR))
         {
            Reset();
            return;
         }

         outL += vL;
         outR += vR;

         voiceLevel *= ampDecay;

         // ampDecay is a slow safety net (see its comment in Trigger()), so
         // waiting on voiceLevel alone to cross -80 dB would hold a voice
         // open for ~8x its knob-set decay. The modal poles and waveguide
         // loop gain actually govern the audible decay and settle to silence
         // near the knob value; free the voice as soon as its output has
         // been silent for a short window, and keep the voiceLevel check
         // only as the ultimate backstop.
         const float mag = std::max(std::fabs(vL), std::fabs(vR));
         if (mag < 5e-5f)
            silentSamples++;
         else
            silentSamples = 0;
         const int silenceWindow = (int)(0.05 * std::max(1.0, voiceSampleRate));
         if ((voiceLevel < 0.0001f || silentSamples >= silenceWindow) && !exciter.active)
         {
            active = false;
            midiNote = -1;
         }
      }
   };
}
