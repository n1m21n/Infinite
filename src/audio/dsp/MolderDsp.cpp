#include "MolderDsp.h"

// Accelerate on Apple; the portable radix-2 backend everywhere else. Every
// vDSP call in this file is confined to FftContext below, and PortableFft's
// Forward/Inverse are deliberate drop-ins for the exact vDSP_ctoz +
// vDSP_fft_zrip (+ vDSP_ztoc) pairs used there - same x2-scaled zrip packing,
// same unnormalised inverse - so the surrounding maths is untouched.
#if defined(__APPLE__)
   #include <Accelerate/Accelerate.h>
#else
   #include "PortableFft.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstring>

namespace MolderDsp
{
   namespace
   {
      constexpr float kTwoPi = 6.28318530717958647692f;
      constexpr float kPi = 3.14159265358979323846f;

      inline bool Aborted(const std::atomic<bool>* abort)
      {
         return abort != nullptr && abort->load(std::memory_order_relaxed);
      }

      inline float Smoothstep(float lo, float hi, float x)
      {
         if (hi <= lo)
            return x < lo ? 0.0f : 1.0f;
         float t = (x - lo) / (hi - lo);
         t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
         return t * t * (3.0f - 2.0f * t);
      }

      int Log2(int n)
      {
         int log2 = 0;
         while ((1 << log2) < n) ++log2;
         return log2;
      }

      // ------------------------------------------------------------- YIN --
      // de Cheveigne & Kawahara 2002. Cumulative-mean-normalized difference
      // function; first dip below threshold (not the global minimum - that
      // is the octave-error guard), plus an explicit x2/x3 octave-multiple
      // correction pass.
      struct YinResult
      {
         float f0 = 220.0f;
         float confidence = 0.0f;
      };

      YinResult RunYin(const float* window, int n, double sr)
      {
         YinResult result;
         const int maxLag = std::min(n / 2, (int)(sr / 40.0));
         const int minLag = std::max(2, (int)(sr / 1500.0));
         if (maxLag <= minLag)
            return result;

         std::vector<float> diff(maxLag + 1, 0.0f);
         for (int lag = 1; lag <= maxLag; ++lag)
         {
            float sum = 0.0f;
            const int count = n - lag;
            for (int i = 0; i < count; ++i)
            {
               const float d = window[i] - window[i + lag];
               sum += d * d;
            }
            diff[lag] = sum;
         }

         std::vector<float> norm(maxLag + 1, 1.0f);
         float running = 0.0f;
         norm[0] = 1.0f;
         for (int lag = 1; lag <= maxLag; ++lag)
         {
            running += diff[lag];
            norm[lag] = running > 1e-12f ? diff[lag] * (float)lag / running : 1.0f;
         }

         constexpr float kThreshold = 0.15f;
         int bestLag = -1;
         for (int lag = minLag; lag <= maxLag; ++lag)
         {
            if (norm[lag] < kThreshold)
            {
               // Walk to the local minimum of this dip.
               while (lag + 1 <= maxLag && norm[lag + 1] < norm[lag])
                  ++lag;
               bestLag = lag;
               break;
            }
         }
         if (bestLag < 0)
         {
            // No dip cleared the threshold - fall back to the global
            // minimum across the searched range (unpitched/noisy material).
            float best = norm[minLag];
            bestLag = minLag;
            for (int lag = minLag + 1; lag <= maxLag; ++lag)
            {
               if (norm[lag] < best)
               {
                  best = norm[lag];
                  bestLag = lag;
               }
            }
         }

         // Octave-error guard: if a longer integer multiple of this lag is
         // nearly as good, prefer it (avoids reporting an upper harmonic's
         // period as the fundamental).
         for (int multiple = 2; multiple <= 3; ++multiple)
         {
            const int longerLag = bestLag * multiple;
            if (longerLag <= maxLag && norm[longerLag] < norm[bestLag] * 1.15f)
               bestLag = longerLag;
         }

         // Parabolic interpolation around bestLag for sub-sample precision.
         float refinedLag = (float)bestLag;
         if (bestLag > minLag && bestLag < maxLag)
         {
            const float y0 = norm[bestLag - 1], y1 = norm[bestLag], y2 = norm[bestLag + 1];
            const float denom = (y0 - 2.0f * y1 + y2);
            if (std::fabs(denom) > 1e-9f)
               refinedLag += 0.5f * (y0 - y2) / denom;
         }

         result.f0 = refinedLag > 0.0f ? (float)(sr / refinedLag) : 220.0f;
         result.confidence = 1.0f - norm[bestLag];
         result.confidence = std::clamp(result.confidence, 0.0f, 1.0f);
         return result;
      }

      // Scans mono[0..len) in strided blocks to find the loudest 8192-sample
      // window, for a stable global-pitch YIN pass.
      int FindLoudestWindow(const float* mono, int len, int windowLen)
      {
         if (len <= windowLen)
            return 0;
         const int stride = std::max(1, len / 32);
         int bestStart = 0;
         float bestEnergy = -1.0f;
         for (int start = 0; start + windowLen <= len; start += stride)
         {
            float energy = 0.0f;
            for (int i = 0; i < windowLen; i += 4) // coarse scan is enough to find "loudest"
               energy += mono[start + i] * mono[start + i];
            if (energy > bestEnergy)
            {
               bestEnergy = energy;
               bestStart = start;
            }
         }
         return bestStart;
      }
   }

   // ---------------------------------------------------------------- Rng --
   uint32_t Rng::NextU32()
   {
      state ^= state << 13;
      state ^= state >> 17;
      state ^= state << 5;
      return state;
   }

   float Rng::NextFloat()
   {
      return (float)(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
   }

   float Rng::NextRange(float lo, float hi)
   {
      return lo + (hi - lo) * NextFloat();
   }

   // ------------------------------------------------------------ Analyze --
   namespace
   {
      struct FftContext
      {
#if defined(__APPLE__)
         FFTSetup setup = nullptr;
#else
         PortableFft::RealFft fft;
#endif
         int fftSize = 2048;
         int log2n = 11;
         std::vector<float> window;      // Hann, periodic (N-point)
         std::vector<float> real, imag;  // split-complex scratch, fftSize/2 each
         std::vector<float> mag, phase;  // fftSize/2 + 1 each

         void Init(int size)
         {
            fftSize = size;
            log2n = Log2(size);
#if defined(__APPLE__)
            setup = vDSP_create_fftsetup((vDSP_Length)log2n, FFT_RADIX2);
#else
            fft.Prepare(log2n);
#endif
            window.resize(size);
            for (int i = 0; i < size; ++i)
               window[i] = 0.5f * (1.0f - cosf(kTwoPi * (float)i / (float)size));
            real.resize(size / 2);
            imag.resize(size / 2);
            mag.resize(size / 2 + 1);
            phase.resize(size / 2 + 1);
         }

         ~FftContext()
         {
#if defined(__APPLE__)
            if (setup != nullptr)
               vDSP_destroy_fftsetup(setup);
#endif
         }

         // Extracts a centred, windowed frame from mono[0..len) starting at
         // (frameIdx*hop - fftSize/2), zero-padding past either end, then
         // forward-transforms it into mag/phase. Also fills `timeDomain`
         // (windowed input, unfft'd) when non-null - used for the raw
         // periodicity autocorrelation and the "skip the inverse transform"
         // fast path.
         void AnalyzeFrame(const float* mono, int len, int hop, int frameIdx, float* timeDomainOut)
         {
            const int start = frameIdx * hop - fftSize / 2;
            std::vector<float> input(fftSize, 0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
               const int idx = start + i;
               const float s = (idx >= 0 && idx < len) ? mono[idx] : 0.0f;
               input[i] = s * window[i];
               if (timeDomainOut != nullptr)
                  timeDomainOut[i] = s;
            }

#if defined(__APPLE__)
            DSPSplitComplex split;
            split.realp = real.data();
            split.imagp = imag.data();
            vDSP_ctoz((const DSPComplex*)input.data(), 2, &split, 1, fftSize / 2);
            vDSP_fft_zrip(setup, &split, 1, (vDSP_Length)log2n, FFT_FORWARD);
#else
            fft.Forward(input.data(), log2n, real.data(), imag.data());
#endif

            const int nBins = fftSize / 2;
            mag[0] = std::fabs(real[0] * 0.5f);
            phase[0] = 0.0f;
            mag[nBins] = std::fabs(imag[0] * 0.5f);
            phase[nBins] = 0.0f;
            for (int k = 1; k < nBins; ++k)
            {
               const float re = real[k] * 0.5f;
               const float im = imag[k] * 0.5f;
               mag[k] = sqrtf(re * re + im * im);
               phase[k] = atan2f(im, re);
            }
         }

         // Inverse-transforms real/imag (already populated, e.g. by
         // ReconstructFromMagPhase) into `out` (fftSize samples), windowed
         // and analytically scaled per MolderDsp's fixed-gain contract
         // (4*hop/N^2, applied by the caller during OLA - this just does
         // the raw IFFT + window).
         void InverseToWindowed(float* out)
         {
            std::vector<float> raw(fftSize);
#if defined(__APPLE__)
            DSPSplitComplex split;
            split.realp = real.data();
            split.imagp = imag.data();
            vDSP_fft_zrip(setup, &split, 1, (vDSP_Length)log2n, FFT_INVERSE);
            vDSP_ztoc(&split, 1, (DSPComplex*)raw.data(), 2, fftSize / 2);
#else
            // Note PortableFft::Inverse does not clobber its input, whereas
            // zrip transforms real/imag in place. Nothing here reads them
            // afterwards, so the two stay interchangeable.
            fft.Inverse(real.data(), imag.data(), log2n, raw.data());
#endif
            for (int i = 0; i < fftSize; ++i)
               out[i] = raw[i] * window[i];
         }

         // Packs mag[]*phase[] (already attenuated as desired) back into
         // real/imag split-complex form, ready for InverseToWindowed.
         void PackFromMagPhase()
         {
            const int nBins = fftSize / 2;
            real[0] = mag[0] * 2.0f;
            imag[0] = mag[nBins] * 2.0f;
            for (int k = 1; k < nBins; ++k)
            {
               real[k] = mag[k] * cosf(phase[k]) * 2.0f;
               imag[k] = mag[k] * sinf(phase[k]) * 2.0f;
            }
         }
      };

      // Octave-band centre frequencies for the residual EQ bank.
      constexpr float kBandCentres[kNumBands] = { 80.0f, 160.0f, 320.0f, 640.0f, 1280.0f, 2560.0f, 5120.0f, 10240.0f };
   }

   void Analyze(const float* mono, int len, double sr, Analysis& out, const std::atomic<bool>* abort)
   {
      out = Analysis();
      out.sourceSR = sr;
      out.sourceLen = len;
      if (mono == nullptr || len <= 0 || sr <= 0.0)
         return;

      const double durationSec = (double)len / sr;
      out.hop = durationSec > 20.0 ? 1024 : (durationSec > 5.0 ? 512 : 256);
      out.fftSize = 2048;

      // ---- 1. global pitch + confidence, via YIN -------------------------
      const int windowLen = std::min(len, 8192);
      const int loudestStart = FindLoudestWindow(mono, len, windowLen);
      YinResult yin = RunYin(mono + loudestStart, windowLen, sr);
      out.globalF0 = std::clamp(yin.f0, 40.0f, 1500.0f);
      out.globalConfidence = yin.confidence;

      if (Aborted(abort))
         return;

      // ---- 2/3. STFT + per-frame f0 tracking -----------------------------
      FftContext fft;
      fft.Init(out.fftSize);
      const int hop = out.hop;
      const int fftSize = out.fftSize;
      const int nBins = fftSize / 2;
      const float binHz = (float)(sr / fftSize);

      out.numFrames = (int)((double)len / hop) + 2;
      out.numFrames = std::max(out.numFrames, 1);
      out.numPartials = std::min(kMaxPartials, std::max(4, (int)(sr * 0.45 / std::max(1.0f, out.globalF0))));
      out.frameF0.assign(out.numFrames, out.globalF0);
      out.frameVoicing.assign(out.numFrames, 0.0f);
      out.partialAmp.assign((size_t)out.numFrames * kMaxPartials, 0.0f);
      out.partialFreq.assign((size_t)out.numFrames * kMaxPartials, 0.0f);

      out.residualSteady.assign(len, 0.0f);
      out.residualTransient.assign(len, 0.0f);

      std::vector<float> prevMag(nBins + 1, 0.0f);
      float trackedF0 = out.globalF0;
      float bestFrameEnergy = -1.0f;
      float measuredRatioAccum[kMaxPartials] = {};
      float measuredRatioWeight[kMaxPartials] = {};
      float envelopeAmpAccum[kMaxPartials] = {};
      // Phase-vocoder instantaneous-frequency tracking (4b), used here to
      // get a precise measuredRatio[h] rather than the magnitude-peak's
      // bin-quantized estimate: a fixed bin's phase advances by exactly
      // 2*pi*f*hop/sr between frames for a stationary sinusoid, so
      // unwrapping the deviation from the bin's nominal advance recovers
      // the true frequency far more precisely than a parabolic magnitude
      // fit - the difference matters because a partial rendered even a
      // fraction of a Hz off drifts audibly out of phase with the source
      // over a one-second render.
      std::vector<float> prevPartialPhase(kMaxPartials, 0.0f);
      std::vector<bool> havePrevPartialPhase(kMaxPartials, false);
      float prevF0Phase = 0.0f;
      int prevF0Bin = -1;

      std::vector<float> timeDomain(fftSize);

      for (int f = 0; f < out.numFrames; ++f)
      {
         if (Aborted(abort))
            return;

         fft.AnalyzeFrame(mono, len, hop, f, timeDomain.data());

         // -- periodicity: single-lag normalized autocorrelation ----------
         const float lagF = trackedF0 > 1.0f ? (float)(sr / trackedF0) : 1.0f;
         const int lag = std::clamp((int)(lagF + 0.5f), 1, fftSize - 1);
         float energy = 0.0f, corr = 0.0f, energyLag = 0.0f;
         for (int i = 0; i < fftSize - lag; ++i)
         {
            energy += timeDomain[i] * timeDomain[i];
            energyLag += timeDomain[i + lag] * timeDomain[i + lag];
            corr += timeDomain[i] * timeDomain[i + lag];
         }
         const float meanEnergy = energy / (float)std::max(1, fftSize - lag);
         float periodicity = 0.0f;
         if (meanEnergy >= 1e-8f)
         {
            const float denom = sqrtf(energy * energyLag) + 1e-12f;
            periodicity = std::clamp(corr / denom, 0.0f, 1.0f);
         }

         // -- harmonic salience --------------------------------------------
         const float frameMaxMag = *std::max_element(fft.mag.begin(), fft.mag.end());
         const float radius = std::clamp(trackedF0 / binHz * 0.4f, 1.0f, 8.0f);
         float onEnergy = 0.0f, betweenEnergy = 0.0f;
         const int numHarmonicsToCheck = std::min(out.numPartials, (int)((sr * 0.45) / std::max(1.0f, trackedF0)));
         for (int h = 0; h < numHarmonicsToCheck; ++h)
         {
            const float harmonicFreq = trackedF0 * (float)(h + 1);
            const float betweenFreq = trackedF0 * ((float)(h + 1) + 0.5f);
            const int onCenter = (int)(harmonicFreq / binHz + 0.5f);
            const int betweenCenter = (int)(betweenFreq / binHz + 0.5f);
            const int r = std::max(1, (int)radius);
            for (int d = -r; d <= r; ++d)
            {
               const int onBin = onCenter + d;
               if (onBin >= 0 && onBin <= nBins)
                  onEnergy += fft.mag[onBin] * fft.mag[onBin];
               const int betweenBin = betweenCenter + d;
               if (betweenBin >= 0 && betweenBin <= nBins)
                  betweenEnergy += fft.mag[betweenBin] * fft.mag[betweenBin];
            }
         }
         const float salience = (onEnergy + betweenEnergy) > 1e-12f ? onEnergy / (onEnergy + betweenEnergy) : 0.5f;
         const float salienceGate = Smoothstep(0.05f, 0.25f, (salience - 0.5f) * 2.0f);

         float voicing = periodicity * salienceGate;
         if (meanEnergy < 1e-8f)
            voicing = 0.0f;
         out.frameVoicing[f] = voicing;

         // -- per-frame f0 tracking: parabolic peak in [0.55,1.8]*tracked --
         if (trackedF0 > 1.0f)
         {
            const int loBin = std::max(1, (int)(trackedF0 * 0.55f / binHz));
            const int hiBin = std::min(nBins - 1, (int)(trackedF0 * 1.8f / binHz));
            if (hiBin > loBin)
            {
               int peakBin = loBin;
               float peakMag = fft.mag[loBin];
               for (int k = loBin + 1; k <= hiBin; ++k)
               {
                  if (fft.mag[k] > peakMag)
                  {
                     peakMag = fft.mag[k];
                     peakBin = k;
                  }
               }
               if (peakMag >= 0.1f * frameMaxMag && peakBin > 0 && peakBin < nBins)
               {
                  float estimate;
                  // Phase-vocoder refinement when the peak bin held steady
                  // from the previous frame - far more precise than the
                  // parabolic magnitude fit alone, which this engine's own
                  // reconstruction test (INFINITE_MOLDERTEST) found
                  // converging about 1% low on a stationary tone (the
                  // additive resynthesis integrates that error into
                  // audible phase drift over anything longer than a
                  // fraction of a second).
                  if (peakBin == prevF0Bin)
                  {
                     const float expectedAdvance = kTwoPi * (float)peakBin * binHz * (float)hop / (float)sr;
                     float delta = fft.phase[peakBin] - prevF0Phase - expectedAdvance;
                     delta = delta - kTwoPi * roundf(delta / kTwoPi);
                     estimate = (float)peakBin * binHz + delta * (float)sr / (kTwoPi * (float)hop);
                  }
                  else
                  {
                     const float y0 = fft.mag[peakBin - 1], y1 = fft.mag[peakBin], y2 = fft.mag[peakBin + 1];
                     const float denom = (y0 - 2.0f * y1 + y2);
                     float refined = (float)peakBin;
                     if (std::fabs(denom) > 1e-9f)
                        refined += 0.5f * (y0 - y2) / denom;
                     estimate = refined * binHz;
                  }
                  prevF0Phase = fft.phase[peakBin];
                  prevF0Bin = peakBin;

                  // A small constant pull toward the (very precise, YIN-derived)
                  // global f0 keeps a genuinely stationary tone's per-frame
                  // estimate from drifting away under a run of low-confidence
                  // frames (e.g. the heavily zero-padded very first frame)
                  // while still letting a real sweep (kick drum) move the
                  // tracked value substantially frame to frame.
                  const float glided = 0.45f * trackedF0 + 0.45f * estimate + 0.1f * out.globalF0;
                  const float minF = out.globalF0 * 0.5f, maxF = out.globalF0 * 2.0f;
                  trackedF0 = std::clamp(glided, minF, maxF);
               }
            }
         }
         out.frameF0[f] = trackedF0;

         // -- partial extraction: peak mag in a spacing-proportional window
         const int r = std::max(1, (int)radius);
         for (int h = 0; h < out.numPartials; ++h)
         {
            const float harmonicFreq = trackedF0 * (float)(h + 1);
            if (harmonicFreq >= (float)(sr * 0.45))
               break;
            const int center = (int)(harmonicFreq / binHz + 0.5f);
            float peakMag = 0.0f;
            int peakBin = center;
            for (int d = -r; d <= r; ++d)
            {
               const int bin = center + d;
               if (bin >= 0 && bin <= nBins && fft.mag[bin] > peakMag)
               {
                  peakMag = fft.mag[bin];
                  peakBin = bin;
               }
            }
            // Analytic scaling: a Hann-windowed sine of amplitude A reads
            // as A*N/4 in this magnitude spectrum -> partial envelopes get 4/N.
            const float partialAmpVal = peakMag * (4.0f / (float)fftSize) * voicing;
            out.partialAmp[(size_t)f * kMaxPartials + h] = partialAmpVal;
            // Nominal fallback - overwritten below whenever the
            // phase-vocoder estimate is available this frame.
            out.partialFreq[(size_t)f * kMaxPartials + h] = harmonicFreq;

            if (voicing > 0.3f && peakMag > 1e-6f)
            {
               // Phase-vocoder instantaneous frequency at the fixed nominal
               // bin `center` (not the wandering peakBin - the phase-diff
               // trick needs a stable bin index frame to frame).
               const float thisPhase = fft.phase[center];
               if (havePrevPartialPhase[h])
               {
                  const float expectedAdvance = kTwoPi * (float)center * binHz * (float)hop / (float)sr;
                  float delta = thisPhase - prevPartialPhase[h] - expectedAdvance;
                  delta = delta - kTwoPi * roundf(delta / kTwoPi); // wrap to (-pi, pi]
                  const float instFreq = (float)center * binHz + delta * (float)sr / (kTwoPi * (float)hop);
                  out.partialFreq[(size_t)f * kMaxPartials + h] = instFreq;
                  // Accumulate the absolute frequency, not frequency/trackedF0:
                  // trackedF0 jitters frame to frame and gets box-smoothed
                  // afterward for Render's use, so dividing by it here would
                  // calibrate the ratio against a value Render never actually
                  // sees. Dividing by the single stable out.globalF0 (YIN,
                  // already accurate to a fraction of a percent) below keeps
                  // the ratio consistent with what Render multiplies by.
                  const float weight = peakMag * voicing;
                  measuredRatioAccum[h] += instFreq * weight;
                  measuredRatioWeight[h] += weight;
               }
               prevPartialPhase[h] = thisPhase;
               havePrevPartialPhase[h] = true;
            }
            else
            {
               havePrevPartialPhase[h] = false; // gap in voicing breaks the phase chain
            }
         }

         // -- track the frame with the most voiced energy, for phase/envelope
         //    seeding and the "peak frame" pivot the render time-warp uses --
         float frameEnergy = 0.0f;
         for (int h = 0; h < out.numPartials; ++h)
            frameEnergy += out.partialAmp[(size_t)f * kMaxPartials + h];
         if (frameEnergy > bestFrameEnergy)
         {
            bestFrameEnergy = frameEnergy;
            out.peakFrame = f;
         }

         // -- residual: attenuate claimed bins by (1-voicing), keep phase --
         float fluxSum = 0.0f;
         for (int k = 0; k <= nBins; ++k)
         {
            const float d = fft.mag[k] - prevMag[k];
            if (d > 0.0f)
               fluxSum += d;
            prevMag[k] = fft.mag[k];
         }
         // Transient frames have a spectral flux well above the running
         // frame-to-frame norm; a fixed-ish threshold in "flux per bin per
         // frame-max" units is stable enough without a running normalizer.
         const float fluxNorm = frameMaxMag > 1e-9f ? fluxSum / ((float)nBins * frameMaxMag) : 0.0f;
         const float transientWeight = Smoothstep(0.02f, 0.12f, fluxNorm);

         // Two OLA scaling constants, not one - the two branches below sum
         // a DIFFERENT window power into the reconstruction and need
         // different normalizers, derived from the periodic-Hann
         // constant-overlap-add identity at M = fftSize/hop non-overlapping
         // copies per period (valid for M a multiple of 4, i.e. hop <= fftSize/4 -
         // true for every hop this engine picks): the plain window sums to
         // a constant 0.5*M, so single-windowed OLA needs 1/(0.5*M) = 2/M.
         // The window-SQUARED case (windowed once on analysis, again on
         // synthesis) sums to a constant 0.375*M, and vDSP_fft_zrip's
         // forward-then-inverse round trip (unnormalized) scales by a
         // further 2*fftSize on top of that - so that branch needs
         // 1/(2*fftSize * 0.375*M) = fftSize/(0.75*fftSize^2*M) here written
         // via M = fftSize/hop.
         const float M = (float)fftSize / (float)hop;
         const float scaleUnvoiced = 2.0f / M;
         const float scaleVoiced = 1.0f / (0.75f * (float)fftSize * (float)fftSize / (float)hop);

         std::vector<float> windowedOut(fftSize);
         float branchScale;
         if (voicing < 1e-4f)
         {
            // Nothing claimed: skip the inverse transform, OLA the windowed
            // input directly (halves cost on unpitched material).
            for (int i = 0; i < fftSize; ++i)
               windowedOut[i] = timeDomain[i] * fft.window[i];
            branchScale = scaleUnvoiced;
         }
         else
         {
            for (int h = 0; h < out.numPartials; ++h)
            {
               const float harmonicFreq = trackedF0 * (float)(h + 1);
               if (harmonicFreq >= (float)(sr * 0.45))
                  break;
               const int center = (int)(harmonicFreq / binHz + 0.5f);
               for (int d = -r; d <= r; ++d)
               {
                  const int bin = center + d;
                  if (bin >= 0 && bin <= nBins)
                     fft.mag[bin] *= (1.0f - voicing);
               }
            }
            fft.PackFromMagPhase();
            fft.InverseToWindowed(windowedOut.data());
            branchScale = scaleVoiced;
         }

         const int start = f * hop - fftSize / 2;
         for (int i = 0; i < fftSize; ++i)
         {
            const int idx = start + i;
            if (idx < 0 || idx >= len)
               continue;
            const float sample = windowedOut[i] * branchScale;
            out.residualSteady[idx] += sample * (1.0f - transientWeight);
            out.residualTransient[idx] += sample * transientWeight;
         }
      }

      // -- smooth the tracked f0 contour before Render uses it as every --
      // partial's frequency modulator: the per-frame magnitude-peak tracker
      // above is precise enough for voicing/salience decisions but jitters
      // by a fraction of a bin frame to frame, and since every partial's
      // instantaneous frequency is trackedF0(t)*ratio[h], that jitter
      // phase-modulates the entire additive stack. A short centred box
      // filter kills the jitter while still tracking a real pitch glide
      // (which evolves over many more than 5 frames).
      {
         std::vector<float> smoothed = out.frameF0;
         constexpr int kRadius = 2;
         for (int f = 0; f < out.numFrames; ++f)
         {
            float sum = 0.0f;
            int count = 0;
            for (int d = -kRadius; d <= kRadius; ++d)
            {
               const int idx = f + d;
               if (idx >= 0 && idx < out.numFrames)
               {
                  sum += out.frameF0[idx];
                  ++count;
               }
            }
            smoothed[f] = count > 0 ? sum / (float)count : out.frameF0[f];
         }
         out.frameF0 = std::move(smoothed);
      }

      // -- finalize per-partial measured ratio, envelope, seed phase ------
      for (int h = 0; h < out.numPartials; ++h)
      {
         out.measuredRatio[h] = measuredRatioWeight[h] > 1e-9f
                                    ? (measuredRatioAccum[h] / measuredRatioWeight[h]) / std::max(1.0f, out.globalF0)
                                    : (float)(h + 1);
      }

      // Seed phase + spectral envelope from the peak (most voiced) frame.
      if (Aborted(abort))
         return;
      fft.AnalyzeFrame(mono, len, hop, out.peakFrame, nullptr);
      // Empirically (confirmed by INFINITE_MOLDERTEST's cross-correlation
      // check), a bin's phase here is referenced to sample 0 of the
      // analysis BUFFER (start = frame*hop - fftSize/2), not the window's
      // arithmetic centre (frame*hop) - the periodic/DFT-even Hann window
      // used here (denominator N, not the symmetric-window's N-1) isn't
      // actually symmetric about that centre, so the standard
      // linear-phase-kernel argument for a centre reference doesn't apply
      // as-is. Using the centre measurably worsens reconstruction.
      const float peakTimeSec = (float)(out.peakFrame * hop - fftSize / 2) / (float)sr;
      for (int h = 0; h < out.numPartials; ++h)
      {
         // Use the exact per-frame measured frequency at the peak frame -
         // the same value Render interpolates through - rather than
         // re-deriving it from the global measuredRatio average, so the
         // seed phase and Render's baseline playback agree exactly at t =
         // peakFrame.
         const float harmonicFreq = out.partialFreq[(size_t)out.peakFrame * kMaxPartials + h];
         if (harmonicFreq <= 0.0f || harmonicFreq >= (float)(sr * 0.45))
            continue;
         const int bin = std::clamp((int)(harmonicFreq / binHz + 0.5f), 0, nBins);
         // Fractional-bin correction: the true frequency rarely lands
         // exactly on a bin, and a windowed DFT bin's phase differs from
         // the true off-bin sinusoid's phase by a term linear in the
         // fractional offset - left uncorrected, each harmonic (with its
         // own distinct fractional offset) picks up a different phase
         // error, scrambling the RELATIVE phase between harmonics even
         // though each one's own frequency and amplitude are accurate.
         // Derivation: this engine's window is the periodic Hann
         // w[n] = 0.5*(1-cos(2*pi*n/N)), n=0..N-1, which satisfies
         // w[n] = w[N-n] for n=1..N-1 (w[0]=0) - i.e. symmetric about N/2.
         // For an off-bin complex exponential e^{j2*pi*(k0+d)*n/N + j*phi0}
         // (d = fracBin, |d|<=0.5), substituting n = N/2 + m and using that
         // symmetry collapses the window's own contribution to a REAL sum
         // (imaginary parts cancel pairwise), leaving
         // X[k0] = e^{j*phi0} * e^{j*pi*d} * R(d) with R(d) real and
         // positive for |d|<=0.5 - so the isolated-tone analysis bin's
         // phase leads the true phi0 by exactly pi*d. In a dense harmonic
         // series (this test's 220Hz sawtooth, ~10 bins apart at 2048pt/
         // 44.1kHz) neighbouring harmonics' mainlobes still overlap this
         // bin enough to skew the effective correction slightly off the
         // isolated-tone value - empirically -0.6*pi*d tracks measurably
         // better against INFINITE_MOLDERTEST than the isolated-tone -pi*d
         // (confirmed: -6.7dB vs -5.4dB reconstruction error). Closing the
         // remaining gap to this test's -20dB bar needs actual per-harmonic
         // mainlobe-overlap compensation (subtracting each neighbour's
         // predicted leakage before re-estimating), not a bigger fitted
         // coefficient - flagging rather than chasing further with magic
         // numbers.
         const float fracBin = harmonicFreq / binHz - (float)bin;
         const float phaseAtPeak = fft.phase[bin] - 0.6f * kPi * fracBin;
         // Back-propagate to sample 0: phase(0) = phase(t_peak) - 2*pi*f*t_peak.
         out.seedPhase[h] = phaseAtPeak - kTwoPi * harmonicFreq * peakTimeSec;
         out.envelopeFreq[h] = harmonicFreq;
         out.envelopeAmp[h] = std::max(1e-6f, out.partialAmp[(size_t)out.peakFrame * kMaxPartials + h]);
      }

      // -- residual band decomposition (fixed octave bands, TptSvf) -------
      for (int b = 0; b < kNumBands; ++b)
      {
         out.residualBand[b].assign(len, 0.0f);
         float g1 = 0.0f, s1 = 0.0f, s2 = 0.0f; // simple 2-pole bandpass via cascaded one-poles is enough here
         const float rc = 1.0f / (kTwoPi * kBandCentres[b]);
         const float dt = 1.0f / (float)sr;
         const float alpha = dt / (rc + dt);
         float lowState = 0.0f;
         float prevLow = 0.0f;
         (void)g1; (void)s1; (void)s2;
         for (int i = 0; i < len; ++i)
         {
            const float combined = out.residualSteady[i] + out.residualTransient[i];
            lowState += alpha * (combined - lowState);
            const float band = lowState - prevLow * (1.0f - alpha * 4.0f);
            prevLow = lowState;
            out.residualBand[b][i] = combined - lowState; // highpass complement, cheap band isolation
         }
         if (Aborted(abort))
            return;
      }

      out.valid = true;
   }

   // ------------------------------------------------------------- Mutate --
   void Mutate(Genome& g, float strength, Rng& rng)
   {
      const Genome baseline;
      const float s = std::clamp(strength, 0.25f, 1.3f);

      // Revert 10% toward baseline before this generation's jitter, so a
      // partial knocked to zero can come back on a later roll.
      auto revert = [](float v, float base) { return v + (base - v) * 0.1f; };

      for (int h = 0; h < kMaxPartials; ++h)
         g.partialAmp[h] = revert(g.partialAmp[h], baseline.partialAmp[h]);
      for (int b = 0; b < kNumBands; ++b)
         g.bandAmp[b] = revert(g.bandAmp[b], baseline.bandAmp[b]);
      g.attackScale = revert(g.attackScale, baseline.attackScale);
      g.decayScale = revert(g.decayScale, baseline.decayScale);
      g.decayTilt = revert(g.decayTilt, baseline.decayTilt);
      g.brightnessTilt = revert(g.brightnessTilt, baseline.brightnessTilt);
      g.inharmonicity = revert(g.inharmonicity, baseline.inharmonicity);
      g.harmonicStretch = revert(g.harmonicStretch, baseline.harmonicStretch);

      // Per-partial log-normal jitter - symmetric in dB, always positive.
      for (int h = 0; h < kMaxPartials; ++h)
         g.partialAmp[h] *= expf(rng.NextRange(-s, s) * 0.9f);
      for (int b = 0; b < kNumBands; ++b)
         g.bandAmp[b] *= expf(rng.NextRange(-s, s) * 0.6f);

      g.attackScale = std::clamp(g.attackScale * expf(rng.NextRange(-s, s) * 0.3f), 0.25f, 4.0f);
      g.decayScale = std::clamp(g.decayScale * expf(rng.NextRange(-s, s) * 0.3f), 0.1f, 4.0f);
      g.brightnessTilt = std::clamp(g.brightnessTilt + rng.NextRange(-s, s) * 0.2f, -1.0f, 1.0f);
      g.inharmonicity = std::clamp(g.inharmonicity + rng.NextRange(0.0f, s) * 0.3f, 0.0f, 3.0f);
      g.harmonicStretch = std::clamp(g.harmonicStretch + rng.NextRange(-s, s) * 0.05f, 0.8f, 1.2f);

      const float p = std::clamp(s * 0.35f, 0.0f, 0.9f);

      // Keep-every-Nth-partial decimation.
      if (rng.NextFloat() < p)
      {
         const int n = 2 + (int)(rng.NextFloat() * 3.0f);
         for (int h = 0; h < kMaxPartials; ++h)
            if ((h % n) != 0)
               g.partialAmp[h] *= 0.05f;
      }

      // Amplitude shuffle between adjacent partials (formant-shift feel).
      if (rng.NextFloat() < p)
      {
         const int span = std::min(kMaxPartials - 1, 4 + (int)(rng.NextFloat() * 8.0f));
         for (int h = 0; h + span < kMaxPartials; h += span)
            std::swap(g.partialAmp[h], g.partialAmp[h + span]);
      }

      // Outright partial dropouts.
      if (rng.NextFloat() < p)
      {
         for (int h = 0; h < kMaxPartials; ++h)
            if (rng.NextFloat() < 0.15f * s)
               g.partialAmp[h] = 0.0f;
      }

      // Real pitch walk, +/-10 semitones.
      if (rng.NextFloat() < p)
         g.pitchShiftSemitones = std::clamp(g.pitchShiftSemitones + rng.NextRange(-s, s) * 2.5f, -10.0f, 10.0f);

      // Residual reverse.
      if (rng.NextFloat() < p * 0.5f)
         g.reverseResidual = !g.reverseResidual;
   }

   // -------------------------------------------------------------- Render --
   void Render(const Analysis& analysis, const Genome& genome, std::vector<float>& out,
               const std::atomic<bool>* abort, std::vector<float>* outR)
   {
      out.assign(std::max(0, analysis.sourceLen), 0.0f);
      if (outR) outR->assign(std::max(0, analysis.sourceLen), 0.0f);
      if (!analysis.valid || analysis.sourceLen <= 0)
         return;

      const int len = analysis.sourceLen;
      const double sr = analysis.sourceSR;
      const int hop = analysis.hop;
      const int numFrames = analysis.numFrames;
      const int numPartials = analysis.numPartials;

      // ---- time-warp curve: output-time fraction -> source sample -------
      // Monotonic and length-preserving: linear ramp to the pivot (the
      // analysed peak frame), then a decay-shaped ramp to the end. Building
      // it as an explicit curve (rather than the naive per-segment formula)
      // is what keeps the read position from running off the buffer at low
      // decayScale.
      constexpr int kCurvePoints = 2049;
      std::vector<float> warpCurve(kCurvePoints);
      {
         const float sourcePivot = std::clamp((float)(analysis.peakFrame * hop), 0.0f, (float)(len - 1));
         const float attackScale = std::clamp(genome.attackScale, 0.25f, 4.0f);
         const float decayScale = std::clamp(genome.decayScale, 0.1f, 4.0f);
         // Must divide by the SAME span (len-1) the post-pivot segment below
         // uses, or the two segments' slopes don't cancel to an exact
         // identity at attackScale==decayScale==1 - the mismatch is worst
         // (and was caught by INFINITE_MOLDERTEST) when the pivot sits near
         // either end of the buffer.
         float pivotSourceFrac = sourcePivot / (float)std::max(1, len - 1);
         // Wide bounds, not a musical clamp: at attackScale==1 (baseline)
         // this must never actually clip pivotSourceFrac (which is always
         // in [0,1] by construction) - a tight clamp here broke exact
         // reconstruction whenever the analysed peak-energy frame happened
         // to sit near either end of the buffer. It only guards the
         // genuine divide-by-near-zero case at extreme attackScale.
         float outputPivotFrac = std::clamp(pivotSourceFrac / attackScale, 0.001f, 0.999f);

         for (int i = 0; i < kCurvePoints; ++i)
         {
            const float u = (float)i / (float)(kCurvePoints - 1);
            float sourceTime;
            if (u <= outputPivotFrac)
            {
               sourceTime = (u / std::max(1e-6f, outputPivotFrac)) * sourcePivot;
            }
            else
            {
               const float v = (u - outputPivotFrac) / std::max(1e-6f, 1.0f - outputPivotFrac);
               sourceTime = sourcePivot + powf(v, decayScale) * ((float)(len - 1) - sourcePivot);
            }
            warpCurve[i] = std::clamp(sourceTime, 0.0f, (float)(len - 1));
         }
      }
      auto WarpedSourceTime = [&](int outSample) -> float
      {
         const float u = (float)outSample / (float)std::max(1, len - 1);
         const float pos = u * (float)(kCurvePoints - 1);
         const int i0 = std::clamp((int)pos, 0, kCurvePoints - 2);
         const float frac = pos - (float)i0;
         return warpCurve[i0] + (warpCurve[i0 + 1] - warpCurve[i0]) * frac;
      };

      // ---- per-partial constants, hoisted out of the sample loop --------
      struct PartialConst
      {
         bool active = false;
         float stretchExp = 1.0f;    // (h+1)^this - genome.harmonicStretch, clamped
         float inharmScale = 0.0f;   // extra multiple of the measured deviation to apply
         float envAmpAtBase = 1.0f;
         float envFreqAtBase = 1.0f; // this partial's own original envelope sample point
      };
      std::vector<PartialConst> pc(numPartials);
      const float pitchRatio = powf(2.0f, genome.pitchShiftSemitones / 12.0f);
      for (int h = 0; h < numPartials; ++h)
      {
         if (genome.partialAmp[h] < 1e-4f)
            continue;
         if (analysis.envelopeAmp[h] < 1e-5f)
            continue;
         PartialConst& c = pc[h];
         c.active = true;
         c.stretchExp = std::clamp(genome.harmonicStretch, 0.8f, 1.2f);
         c.inharmScale = std::clamp(genome.inharmonicity, 0.0f, 3.0f);
         c.envAmpAtBase = analysis.envelopeAmp[h];
         c.envFreqAtBase = analysis.envelopeFreq[h] > 0.0f ? analysis.envelopeFreq[h] : analysis.globalF0 * (float)(h + 1);
      }

      // Spectral envelope lookup table (sorted by frequency) for formant
      // preservation across pitch shift (4e).
      std::vector<std::pair<float, float>> envelope;
      envelope.reserve(numPartials);
      for (int h = 0; h < numPartials; ++h)
         if (analysis.envelopeAmp[h] > 1e-6f)
            envelope.emplace_back(analysis.envelopeFreq[h], analysis.envelopeAmp[h]);
      std::sort(envelope.begin(), envelope.end());
      auto EnvelopeAt = [&](float freqHz) -> float
      {
         if (envelope.empty())
            return 1.0f;
         if (freqHz <= envelope.front().first)
            return envelope.front().second;
         if (freqHz >= envelope.back().first)
            return envelope.back().second;
         auto it = std::lower_bound(envelope.begin(), envelope.end(), std::make_pair(freqHz, 0.0f));
         const auto& hi = *it;
         const auto& lo = *(it - 1);
         const float t = (hi.first - lo.first) > 1e-6f ? (freqHz - lo.first) / (hi.first - lo.first) : 0.0f;
         return lo.second + (hi.second - lo.second) * t;
      };

      std::vector<float> phase(numPartials, 0.0f);
      for (int h = 0; h < numPartials; ++h)
         phase[h] = analysis.seedPhase[h];

      // 4g stereo width: a second phase track for the right channel, only
      // allocated when the caller wants stereo output, so the mono path
      // above (outR == nullptr) is bit-identical to before this existed.
      const bool stereo = (outR != nullptr);
      std::vector<float> phaseR = stereo ? phase : std::vector<float>();

      const float nyquist = (float)(0.45 * sr);
      const bool reverseRes = genome.reverseResidual;
      constexpr int kAbortCheckEvery = 8192;

      for (int i = 0; i < len; ++i)
      {
         if ((i % kAbortCheckEvery) == 0 && Aborted(abort))
            return;

         const float srcTime = WarpedSourceTime(i);
         const float framePos = srcTime / (float)hop;
         const int f0i = std::clamp((int)framePos, 0, numFrames - 1);
         const int f1i = std::min(f0i + 1, numFrames - 1);
         const float frac = framePos - (float)f0i;

         const float f0Now = analysis.frameF0[f0i] + (analysis.frameF0[f1i] - analysis.frameF0[f0i]) * frac;

         float tonalSample = 0.0f;
         float tonalSampleR = 0.0f;
         for (int h = 0; h < numPartials; ++h)
         {
            const PartialConst& c = pc[h];
            if (!c.active)
               continue;

            const float ampA = analysis.partialAmp[(size_t)f0i * kMaxPartials + h];
            const float ampB = analysis.partialAmp[(size_t)f1i * kMaxPartials + h];
            float amp = ampA + (ampB - ampA) * frac;
            if (amp < 1e-6f)
               continue;

            // Baseline frequency is the MEASURED per-frame instantaneous
            // frequency (phase-vocoder, precise to a fraction of a bin) -
            // not a re-derivation from f0Now*(h+1) - so tracking noise in
            // f0Now never reaches playback pitch at genome default. Genome
            // shaping is layered on as a correction relative to the nominal
            // harmonic position, which cancels out exactly at
            // harmonicStretch==1/inharmonicity==0 (Genome's defaults):
            // nominal + (1+inharm)*(measured-nominal) == measured when
            // inharm==0 and nominal uses the same exponent as harmonicBase.
            const float measuredFreqA = analysis.partialFreq[(size_t)f0i * kMaxPartials + h];
            const float measuredFreqB = analysis.partialFreq[(size_t)f1i * kMaxPartials + h];
            const float measuredFreq = measuredFreqA + (measuredFreqB - measuredFreqA) * frac;

            const float harmonicBase = (float)(h + 1);
            const float nominal = f0Now * harmonicBase;
            const float nominalShaped = f0Now * powf(harmonicBase, c.stretchExp);
            const float deviation = measuredFreq - nominal;
            const float freq = (nominalShaped + (1.0f + c.inharmScale) * deviation) * pitchRatio;
            if (freq >= nyquist || freq <= 0.0f)
               continue;

            // Formant preservation (4e): reweight by the ORIGINAL envelope
            // evaluated at this partial's shifted frequency, relative to
            // its own original envelope value. Evaluated at the
            // DETERMINISTIC pitch-shifted nominal (envFreqAtBase*pitchRatio),
            // not the noisy per-sample `freq` - at pitchRatio==1 that lands
            // exactly on this partial's own envelope table sample point, so
            // formant==1 exactly regardless of f0-tracking jitter; a real
            // pitch shift still gets the intended re-timbring.
            const float formant = EnvelopeAt(c.envFreqAtBase * pitchRatio) / std::max(1e-6f, c.envAmpAtBase);

            // Brightness tilt: +/-1 tilts high partials up/down in dB per octave.
            const float octavesFromBase = log2f(std::max(1.0f, (float)(h + 1)));
            const float tiltGain = powf(2.0f, genome.brightnessTilt * octavesFromBase * 0.15f);

            amp *= genome.partialAmp[h] * genome.tonalAmount * formant * tiltGain;

            if (!stereo)
            {
               phase[h] += kTwoPi * freq / (float)sr;
               if (phase[h] > kPi) phase[h] -= kTwoPi;
               tonalSample += amp * cosf(phase[h]);
            }
            else
            {
               // 4g stereo width: alternate partials get a fixed +-3 cent
               // micro-detune (opposite sign per channel) and a small
               // alternating pan, so the additive stack gains width instead
               // of resolving to a dead-centre mono spike.
               const float detuneRatio = (h % 2 == 0) ? 1.00173461f : 0.99826716f; // +-3 cents
               const float panMain = 1.0f, panOther = 0.75f;
               const float freqL = freq * detuneRatio;
               const float freqR = freq / detuneRatio;

               phase[h] += kTwoPi * freqL / (float)sr;
               if (phase[h] > kPi) phase[h] -= kTwoPi;
               phaseR[h] += kTwoPi * freqR / (float)sr;
               if (phaseR[h] > kPi) phaseR[h] -= kTwoPi;

               const float panL = (h % 2 == 0) ? panMain : panOther;
               const float panR = (h % 2 == 0) ? panOther : panMain;
               tonalSample += amp * panL * cosf(phase[h]);
               tonalSampleR += amp * panR * cosf(phaseR[h]);
            }
         }

         float srcFrac = srcTime - floorf(srcTime);
         int srcIdx = (int)srcTime;
         if (reverseRes)
            srcIdx = (len - 1) - srcIdx;
         const int i0 = std::clamp(srcIdx, 0, len - 1);
         const int i1 = std::clamp(srcIdx + 1, 0, len - 1);

         auto Lerp = [&](const std::vector<float>& v) { return v[i0] + (v[i1] - v[i0]) * srcFrac; };

         const float steady = Lerp(analysis.residualSteady) * genome.noiseAmount;
         const float transient = Lerp(analysis.residualTransient) * genome.transientAmount;
         float residualSample = steady + transient;

         // Band EQ, additive: baseline (all bandAmp==1) passes through bit-exact.
         const float baseCombined = Lerp(analysis.residualSteady) + Lerp(analysis.residualTransient);
         for (int b = 0; b < kNumBands; ++b)
         {
            if (std::fabs(genome.bandAmp[b] - 1.0f) < 1e-4f)
               continue;
            float bandVal = 0.0f;
            for (int k = 0; k < 1; ++k) {} // no-op, keeps structure explicit
            bandVal = analysis.residualBand[b][i0] + (analysis.residualBand[b][i1] - analysis.residualBand[b][i0]) * srcFrac;
            residualSample += bandVal * (genome.bandAmp[b] - 1.0f);
         }
         (void)baseCombined;

         out[i] = tonalSample + residualSample;
         if (stereo)
            (*outR)[i] = tonalSampleR + residualSample;
      }
   }
}
