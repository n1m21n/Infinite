#include "DrumClassifier.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "PortableFft.h"

namespace DrumClassifier
{
   const char* ClassName(DrumClass c)
   {
      switch (c)
      {
         case DrumClass::Kick:      return "Kick";
         case DrumClass::Bass:      return "Bass";
         case DrumClass::Snare:     return "Snare";
         case DrumClass::Clap:      return "Clap";
         case DrumClass::HatClosed: return "HatClosed";
         case DrumClass::HatOpen:   return "HatOpen";
         case DrumClass::Perc:      return "Perc";
         case DrumClass::Synth:     return "Synth";
         case DrumClass::Piano:     return "Piano";
         case DrumClass::Tonal:     return "Tonal";
      }
      return "Perc";
   }

   DrumClass ClassFromName(const char* name)
   {
      if (name == nullptr)
         return DrumClass::Perc;
      for (int i = 0; i < kNumClasses; i++)
      {
         if (strcasecmp(name, ClassName((DrumClass)i)) == 0)
            return (DrumClass)i;
      }
      return DrumClass::Perc;
   }

   namespace
   {
      // ---------------------------------------------------------- tokenizer
      // Splits a filename on non-alphanumeric characters AND case boundaries
      // (camelCase/PascalCase), lower-cased. Whole-token match only, so
      // "sub" never matches inside "subtle" and "hat" never matches inside
      // "what" (fix D2).
      std::vector<std::string> Tokenize(const std::string& name)
      {
         std::vector<std::string> tokens;
         std::string cur;
         auto flush = [&]()
         {
            if (!cur.empty())
            {
               tokens.push_back(cur);
               cur.clear();
            }
         };

         for (size_t i = 0; i < name.size(); i++)
         {
            const unsigned char c = (unsigned char)name[i];
            if (!std::isalnum(c))
            {
               flush();
               continue;
            }
            // Case boundary: lower->upper (camelCase) starts a new token.
            if (!cur.empty() && std::islower((unsigned char)cur.back()) && std::isupper(c))
               flush();
            cur.push_back((char)std::tolower(c));
         }
         flush();
         return tokens;
      }

      bool HasToken(const std::vector<std::string>& tokens, const char* tok)
      {
         for (const auto& t : tokens)
         {
            if (t == tok)
               return true;
         }
         return false;
      }

      bool HasAnyToken(const std::vector<std::string>& tokens, std::initializer_list<const char*> list)
      {
         for (const char* tok : list)
         {
            if (HasToken(tokens, tok))
               return true;
         }
         return false;
      }

      // ------------------------------------------------------------ one-pole
      // Simple one-pole low-pass, coefficient set directly from a cutoff in
      // Hz (not DspMath::OnePole's time-constant form, which this classifier
      // doesn't need). Two passes give a 2nd-order (~12 dB/oct) rolloff.
      void LowpassInPlace(std::vector<float>& buf, float cutoffHz, double sr)
      {
         if (buf.empty() || cutoffHz <= 0.0f)
            return;
         const float coeff = std::exp(-2.0f * 3.14159265358979323846f * cutoffHz / (float)sr);
         float y = buf[0];
         for (float& x : buf)
         {
            y = x + (y - x) * coeff;
            x = y;
         }
      }

      std::vector<float> Lowpass2(const float* mono, int len, float cutoffHz, double sr)
      {
         std::vector<float> out(mono, mono + len);
         LowpassInPlace(out, cutoffHz, sr);
         LowpassInPlace(out, cutoffHz, sr);
         return out;
      }

      float MeanPower(const float* data, int len)
      {
         if (len <= 0)
            return 0.0f;
         double sum = 0.0;
         for (int i = 0; i < len; i++)
            sum += (double)data[i] * (double)data[i];
         return (float)(sum / len);
      }

      // Normalised autocorrelation peak search over [minLag, maxLag], with an
      // octave-error guard (D5): scan ascending and take the FIRST lag whose
      // correlation is within 0.8x of the running maximum, rather than the
      // (possibly sub-harmonic) global maximum.
      struct CorrPeak
      {
         int lag = 0;
         float value = 0.0f;
      };

      CorrPeak PeakCorrelation(const float* mono, int offset, int corrLen, int minLag, int maxLag)
      {
         CorrPeak result;
         if (corrLen <= 0 || maxLag <= minLag)
            return result;

         float e0 = 0.0f;
         for (int i = 0; i < corrLen; i++)
            e0 += mono[offset + i] * mono[offset + i];
         if (e0 < 1e-9f)
            return result;

         std::vector<float> r(maxLag - minLag + 1, 0.0f);
         float maxR = 0.0f;
         for (int lag = minLag; lag <= maxLag; lag++)
         {
            float cross = 0.0f, eLag = 0.0f;
            for (int i = 0; i < corrLen; i++)
            {
               cross += mono[offset + i] * mono[offset + i + lag];
               eLag += mono[offset + i + lag] * mono[offset + i + lag];
            }
            const float denom = std::sqrt(e0 * eLag);
            const float normR = denom > 1e-9f ? cross / denom : 0.0f;
            r[lag - minLag] = normR;
            maxR = std::max(maxR, normR);
         }

         if (maxR <= 0.0f)
            return result;

         for (int lag = minLag; lag <= maxLag; lag++)
         {
            if (r[lag - minLag] >= 0.8f * maxR)
            {
               result.lag = lag;
               result.value = r[lag - minLag];
               return result;
            }
         }
         result.lag = minLag;
         result.value = maxR;
         return result;
      }
   } // namespace

   Result Classify(const float* mono, int len, double sr, const char* fileNameHint)
   {
      Result res;
      // D9: silent/empty/invalid input returns Perc with scores[Perc] = 1,
      // same as the normal no-signal fallback below.
      if (mono == nullptr || len <= 0 || sr <= 1000.0)
      {
         res.scores[(int)DrumClass::Perc] = 1.0f;
         res.cls = DrumClass::Perc;
         return res;
      }

      Features& f = res.f;

      // 1. Envelope analysis: peak, attack time
      float peakVal = 0.0f;
      int peakIdx = 0;
      for (int i = 0; i < len; i++)
      {
         const float a = std::fabs(mono[i]);
         if (a > peakVal)
         {
            peakVal = a;
            peakIdx = i;
         }
      }

      if (peakVal < 1e-5f)
      {
         res.scores[(int)DrumClass::Perc] = 1.0f; // D9
         res.cls = DrumClass::Perc;
         return res;
      }

      const float v10 = 0.10f * peakVal;
      const float v90 = 0.90f * peakVal;
      int t10 = 0;
      int t90 = peakIdx;
      for (int i = 0; i <= peakIdx; i++)
      {
         if (std::fabs(mono[i]) >= v10)
         {
            t10 = i;
            break;
         }
      }
      for (int i = t10; i <= peakIdx; i++)
      {
         if (std::fabs(mono[i]) >= v90)
         {
            t90 = i;
            break;
         }
      }
      f.attackTimeMs = (float)((t90 - t10) * 1000.0 / sr);

      // Decay: RMS envelope, window >= one period of 30 Hz (~33 ms) - fix D6
      // (was a 3 ms peak-of-window, too short to be a stable RMS estimate).
      const int smoothWin = std::max(1, (int)(sr * 0.033));
      int tDrop = len;
      std::vector<float> envelope; // for decayLinearityR2 / hasSustainPlateau below
      envelope.reserve((len - peakIdx) / std::max(1, smoothWin) + 2);
      for (int i = peakIdx; i < len; i += smoothWin)
      {
         const int end = std::min(len, i + smoothWin);
         const float rms = std::sqrt(MeanPower(mono + i, end - i));
         envelope.push_back(rms);
         if (rms < v10 && tDrop == len)
            tDrop = i;
      }
      f.decayTimeMs = (float)((tDrop - peakIdx) * 1000.0 / sr);
      if (getenv("BADEBUG2") != nullptr)
      {
         printf("  DEBUG2 peakIdx=%d peakVal=%.4f len=%d tDrop=%d v10=%.4f envSize=%zu\n", peakIdx, peakVal, len,
                tDrop, v10, envelope.size());
         for (size_t i = 0; i < envelope.size() && i < 15; i++)
            printf("    env[%zu]=%.4f\n", i, envelope[i]);
      }

      // decayLinearityR2: linear regression of log(envelope) vs window index,
      // over the decaying region only (drop leading/trailing near-zero bins).
      // High R^2 = smooth exponential decay (Piano cue). hasSustainPlateau:
      // low relative variance across the middle of the envelope (Synth cue).
      {
         std::vector<double> xs, ys;
         for (size_t i = 0; i < envelope.size(); i++)
         {
            if (envelope[i] > 1e-6f)
            {
               xs.push_back((double)i);
               ys.push_back(std::log((double)envelope[i]));
            }
         }
         if (xs.size() >= 3)
         {
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            const double n = (double)xs.size();
            for (size_t i = 0; i < xs.size(); i++)
            {
               sx += xs[i];
               sy += ys[i];
               sxx += xs[i] * xs[i];
               sxy += xs[i] * ys[i];
            }
            const double denom = n * sxx - sx * sx;
            if (std::fabs(denom) > 1e-9)
            {
               const double slope = (n * sxy - sx * sy) / denom;
               const double intercept = (sy - slope * sx) / n;
               double ssTot = 0, ssRes = 0;
               const double meanY = sy / n;
               for (size_t i = 0; i < xs.size(); i++)
               {
                  const double pred = intercept + slope * xs[i];
                  ssRes += (ys[i] - pred) * (ys[i] - pred);
                  ssTot += (ys[i] - meanY) * (ys[i] - meanY);
               }
               f.decayLinearityR2 = ssTot > 1e-9 ? (float)std::clamp(1.0 - ssRes / ssTot, 0.0, 1.0) : 0.0f;
            }
         }

         if (envelope.size() >= 4)
         {
            const size_t midStart = envelope.size() / 4;
            const size_t midEnd = (envelope.size() * 3) / 4;
            if (midEnd > midStart + 1)
            {
               double mean = 0.0;
               for (size_t i = midStart; i < midEnd; i++)
                  mean += envelope[i];
               mean /= (double)(midEnd - midStart);
               double var = 0.0;
               for (size_t i = midStart; i < midEnd; i++)
                  var += (envelope[i] - mean) * (envelope[i] - mean);
               var /= (double)(midEnd - midStart);
               const double relStd = mean > 1e-9 ? std::sqrt(var) / mean : 1.0;
               f.hasSustainPlateau = mean > 0.15 * peakVal && relStd < 0.25;
            }
         }
      }

      // 2. Zero-crossing rate
      int zcrCount = 0;
      for (int i = 0; i < len - 1; i++)
      {
         if ((mono[i] >= 0.0f && mono[i + 1] < 0.0f) || (mono[i] < 0.0f && mono[i + 1] >= 0.0f))
            zcrCount++;
      }
      f.zcr = (float)zcrCount / (float)std::max(1, len - 1);

      // 3. Micro-onset count in first 40 ms (fix D3): flux ODF at a 128-sample
      // hop, frame-0 onset always counted, subsequent peaks counted only when
      // 5-15 ms from the last counted peak (real clap burst spacing) and
      // above an adaptive threshold - so a single continuous transient
      // (all its energy inside one burst) reports 1, not several.
      {
         const int n40 = std::min(len, (int)(sr * 0.040));
         const int hopMicro = 128;
         const int numMicroFrames = n40 / hopMicro;
         const double hopMs = 1000.0 * hopMicro / sr;

         if (numMicroFrames >= 2)
         {
            std::vector<float> energy(numMicroFrames, 0.0f);
            for (int m = 0; m < numMicroFrames; m++)
               energy[m] = MeanPower(mono + m * hopMicro, hopMicro);

            std::vector<float> flux(numMicroFrames, 0.0f);
            float maxFlux = 0.0f;
            for (int m = 1; m < numMicroFrames; m++)
            {
               flux[m] = std::max(0.0f, energy[m] - energy[m - 1]);
               maxFlux = std::max(maxFlux, flux[m]);
            }

            int microCount = 1; // frame-0 onset always counts
            double lastPeakMs = 0.0;
            const float thresh = 0.15f * maxFlux;
            for (int m = 1; m < numMicroFrames - 1; m++)
            {
               if (flux[m] > thresh && flux[m] >= flux[m - 1] && flux[m] >= flux[m + 1])
               {
                  const double tMs = m * hopMs;
                  const double gapMs = tMs - lastPeakMs;
                  if (gapMs >= 5.0 && gapMs <= 15.0)
                  {
                     microCount++;
                     lastPeakMs = tMs;
                  }
                  else if (gapMs > 15.0)
                  {
                     // Too far to be part of this burst sequence; don't fold
                     // it in, but track it so a later, closer peak still has
                     // a sane reference point.
                     lastPeakMs = tMs;
                  }
                  // gapMs < 5: same burst as the last accepted peak - skip.
               }
            }
            f.microOnsets40ms = microCount;
            if (getenv("BADEBUG3") != nullptr)
            {
               printf("  DEBUG3 numMicroFrames=%d maxFlux=%.6f thresh=%.6f hopMs=%.3f\n", numMicroFrames, maxFlux,
                      thresh, hopMs);
               for (int m = 0; m < numMicroFrames; m++)
                  printf("    m=%d energy=%.6f flux=%.6f\n", m, energy[m], flux[m]);
            }
         }
         else
         {
            f.microOnsets40ms = 1;
         }
      }

      // 4. Low-band pitch stability (30-250 Hz), fixes D4 + D5:
      //    - low-passed (~300 Hz, x2) before correlating, so a broadband
      //      transient doesn't fake a low-band period;
      //    - octave-error guard (first peak >= 0.8x max);
      //    - needs >= 2 correlation windows, else reports "unknown" (0), not
      //      a single window's (unreliable) correlation.
      {
         const std::vector<float> lowSignal = Lowpass2(mono, len, 300.0f, sr);
         const int minLag = (int)std::floor(sr / 250.0);
         const int maxLag = (int)std::ceil(sr / 30.0);

         // Shrink the window for short slices rather than falling back to a
         // single unreliable window (D4): use the largest window that still
         // lets two non-overlapping windows fit, down to a lag-limited floor.
         int corrLen = std::min((int)(sr * 0.150), (len - maxLag) / 2);
         if (corrLen > maxLag && maxLag > minLag && corrLen >= maxLag * 2)
         {
            const auto c1 = PeakCorrelation(lowSignal.data(), 0, corrLen, minLag, maxLag);
            const auto c2 = PeakCorrelation(lowSignal.data(), corrLen, corrLen, minLag, maxLag);
            if (c1.value > 0.5f && c2.value > 0.5f && std::abs(c1.lag - c2.lag) <= 4)
               f.lowBandPitchStability = std::clamp(0.5f * (c1.value + c2.value), 0.0f, 1.0f);
            else
               f.lowBandPitchStability = 0.0f;
         }
         else
         {
            f.lowBandPitchStability = 0.0f; // too short for 2 windows: unknown
         }
      }

      // 5. Wideband pitchedness (80-2000 Hz), for Synth/Piano/Tonal:
      //    normalised-autocorrelation f0 estimate (YIN-style peak picking,
      //    de Cheveigne & Kawahara 2002) with the same octave-error guard.
      {
         const int minLag = (int)std::floor(sr / 2000.0);
         const int maxLag = (int)std::ceil(sr / 80.0);
         const int corrLen = std::min(len - maxLag, (int)(sr * 0.060));
         if (corrLen > maxLag && maxLag > minLag)
         {
            const auto peak = PeakCorrelation(mono, 0, corrLen, minLag, maxLag);
            if (peak.value > 0.35f && peak.lag > 0)
            {
               f.f0Hz = (float)(sr / peak.lag);
               f.f0Confidence = peak.value;
            }
         }
      }

      // 6. STFT feature extraction (1024-pt Hann STFT over first ~250 ms):
      // spectral centroid, spectral flatness, and harmonic-peak ratio /
      // inharmonicity stretch when a candidate f0 exists. Band energies are
      // NOT computed here (fix D8: no FFT-bin-count dependency on sample
      // rate) - see the time-domain filter bank below instead.
      constexpr int kFftSize = 1024;
      constexpr int kLog2N = 10;
      constexpr int kHop = 256;
      const int maxAnalysisLen = std::min(len, (int)(sr * 0.250));
      const int numFrames = std::max(1, (maxAnalysisLen + kHop - 1) / kHop);

      PortableFft::RealFft fft;
      if (fft.Prepare(kLog2N))
      {
         std::vector<float> window(kFftSize);
         PortableFft::HannWindowNorm(window.data(), kFftSize);

         const int numBins = kFftSize / 2;
         const float binHz = (float)sr / (float)kFftSize;
         const float magScale = 2.0f / (float)kFftSize;

         std::vector<float> frame(kFftSize);
         std::vector<float> re(numBins), im(numBins);
         std::vector<float> mag(numBins, 0.0f);
         std::vector<float> avgPower(numBins, 0.0f);
         std::vector<float> avgMag(numBins, 0.0f);
         std::vector<float> centroids(numFrames, 0.0f);

         float totalEnergy = 0.0f;
         float centroidSum = 0.0f;
         int centroidFrames = 0;

         for (int n = 0; n < numFrames; n++)
         {
            const int base = n * kHop;
            for (int i = 0; i < kFftSize; i++)
            {
               const int src = base + i;
               frame[i] = (src < len) ? mono[src] * window[i] : 0.0f;
            }

            fft.Forward(frame.data(), kLog2N, re.data(), im.data());

            mag[0] = std::fabs(re[0]) * magScale;
            float numC = 0.0f, denC = mag[0];
            avgPower[0] += mag[0] * mag[0];
            avgMag[0] += mag[0];

            for (int k = 1; k < numBins; k++)
            {
               const float m = std::sqrt(re[k] * re[k] + im[k] * im[k]) * magScale;
               mag[k] = m;
               avgPower[k] += m * m;
               avgMag[k] += m;

               const float fHz = k * binHz;
               numC += fHz * m;
               denC += m;
               totalEnergy += m * m;
            }

            const float c = denC > 1e-6f ? numC / denC : 0.0f;
            centroids[n] = c;
            if (denC > 1e-6f)
            {
               centroidSum += c;
               centroidFrames++;
            }
         }

         if (centroidFrames > 0)
            f.centroidMean = centroidSum / (float)centroidFrames;

         const int frames50ms = std::min(numFrames, std::max(1, (int)(sr * 0.050 / kHop)));
         if (frames50ms >= 3)
         {
            const float cStart = centroids[0];
            const float cEnd = centroids[frames50ms - 1];
            f.centroidTrajectory = cEnd - cStart;
            // Fix D7: relative drop, not an absolute-Hz threshold.
            f.centroidDropRatio = cStart > 1.0f ? (cStart - cEnd) / cStart : 0.0f;
         }

         // Spectral flatness (Wiener entropy: geometric mean / arithmetic mean of power)
         double logSum = 0.0, linSum = 0.0;
         int usedBins = 0;
         for (int k = 1; k < numBins; k++)
         {
            const float p = avgPower[k] / (float)numFrames;
            logSum += std::log(p + 1e-12);
            linSum += (p + 1e-12);
            usedBins++;
         }
         if (usedBins > 0 && linSum > 1e-9)
         {
            const double geomMean = std::exp(logSum / usedBins);
            const double arithMean = linSum / usedBins;
            f.spectralFlatness = (float)std::clamp(geomMean / arithMean, 0.0, 1.0);
         }

         // Harmonic-peak ratio + inharmonicity stretch, if f0 was found.
         if (f.f0Hz > 20.0f && totalEnergy > 1e-9f)
         {
            const int maxHarm = std::min(8, (int)((float)(numBins - 1) * binHz / f.f0Hz));
            float harmEnergy = 0.0f;
            double stretchSum = 0.0;
            int stretchCount = 0;
            for (int k = 1; k <= std::max(1, maxHarm); k++)
            {
               const float idealHz = f.f0Hz * (float)k;
               const int idealBin = (int)std::round(idealHz / binHz);
               const int searchRadius = std::max(1, (int)(0.15f * idealHz / binHz));
               int bestBin = idealBin;
               float bestMag = 0.0f;
               for (int b = std::max(1, idealBin - searchRadius); b <= std::min(numBins - 1, idealBin + searchRadius); b++)
               {
                  const float m = avgMag[b] / (float)numFrames;
                  if (m > bestMag)
                  {
                     bestMag = m;
                     bestBin = b;
                  }
               }
               harmEnergy += bestMag * bestMag;
               if (k >= 2)
               {
                  const float measuredHz = bestBin * binHz;
                  const double relDev = (idealHz > 1.0f) ? (double)((measuredHz - idealHz) / idealHz) : 0.0;
                  stretchSum += std::max(0.0, relDev); // stretch = sharper than ideal
                  stretchCount++;
               }
            }
            f.harmonicRatio = std::clamp(harmEnergy / totalEnergy, 0.0f, 1.0f);
            if (stretchCount > 0)
               f.inharmonicityStretch = (float)std::clamp((stretchSum / stretchCount) * 20.0, 0.0, 1.0);
         }
      }

      // Time-domain filter-bank band energies (fix D8: sample-rate
      // independent, no FFT sub-band bin-count problem).
      {
         auto bandPower = [&](float loHz, float hiHz) -> float
         {
            std::vector<float> hp(mono, mono + len);
            if (loHz > 1.0f)
            {
               std::vector<float> loPart = Lowpass2(mono, len, loHz, sr);
               for (int i = 0; i < len; i++)
                  hp[i] -= loPart[i];
            }
            if (hiHz > 0.0f)
               LowpassInPlace(hp, hiHz, sr), LowpassInPlace(hp, hiHz, sr);
            return MeanPower(hp.data(), len);
         };

         const float pSub = bandPower(0.0f, 90.0f);
         const float pLow = bandPower(90.0f, 250.0f);
         const float pLowMid = bandPower(250.0f, 1000.0f);
         const float pMid = bandPower(1000.0f, 4000.0f);
         const float pHigh = bandPower(4000.0f, 10000.0f);
         std::vector<float> lp10k = Lowpass2(mono, len, 10000.0f, sr);
         std::vector<float> airSig(len);
         for (int i = 0; i < len; i++)
            airSig[i] = mono[i] - lp10k[i];
         const float pAir = MeanPower(airSig.data(), len);

         const float total = pSub + pLow + pLowMid + pMid + pHigh + pAir;
         if (total > 1e-9f)
         {
            f.energySub = pSub / total;
            f.energyLow = pLow / total;
            f.energyLowMid = pLowMid / total;
            f.energyMid = pMid / total;
            f.energyHigh = pHigh / total;
            f.energyAir = pAir / total;
         }
      }

      // 7. Decision rule: weighted scores per class
      float sKick = 0.0f, sBass = 0.0f, sSnare = 0.0f, sClap = 0.0f;
      float sHatClosed = 0.0f, sHatOpen = 0.0f, sPerc = 0.0f;
      float sSynth = 0.0f, sPiano = 0.0f, sTonal = 0.0f;

      const float lowRatio = f.energySub + f.energyLow;
      const float highRatio = f.energyHigh + f.energyAir;

      // ---- Kick rules (fix D7: centroid penalty + relative drop) ----
      sKick += lowRatio * 4.0f;
      if (f.centroidMean < 300.0f)
         sKick += 2.5f;
      else if (f.centroidMean < 600.0f)
         sKick += 1.0f;
      if (f.centroidMean > 1500.0f)
         sKick -= 4.0f; // a snare's noise band must never read as Kick

      if (f.centroidDropRatio > 0.20f)
         sKick += 2.0f; // pitch really fell, not just a small absolute wobble

      if (f.decayTimeMs < 250.0f)
         sKick += 2.0f;
      else if (f.decayTimeMs > 400.0f)
         sKick -= 2.0f;

      if (f.spectralFlatness < 0.05f)
         sKick += 1.5f;

      if (f.microOnsets40ms > 1 && f.centroidMean > 800.0f)
         sKick -= 2.0f;

      // ---- Bass rules ----
      sBass += lowRatio * 3.5f;
      if (f.decayTimeMs > 400.0f)
         sBass += 3.5f;
      else if (f.decayTimeMs > 250.0f)
         sBass += 1.5f;

      if (f.lowBandPitchStability > 0.5f)
         sBass += f.lowBandPitchStability * 3.0f;

      if (f.spectralFlatness < 0.08f)
         sBass += 1.5f;
      if (f.centroidMean < 800.0f)
         sBass += 1.5f;
      if (f.microOnsets40ms > 1 && f.centroidMean > 800.0f)
         sBass -= 2.0f;

      // ---- Snare rules ----
      sSnare += (f.energyMid * 3.0f + f.energyLowMid * 2.0f);
      if (f.centroidMean >= 800.0f && f.centroidMean <= 4500.0f)
         sSnare += 2.5f;
      if (f.spectralFlatness >= 0.05f && f.spectralFlatness <= 0.7f)
         sSnare += 1.5f;
      if (f.decayTimeMs >= 50.0f && f.decayTimeMs <= 350.0f)
         sSnare += 1.5f;
      if (f.microOnsets40ms <= 1)
         sSnare += 1.5f;
      else
         sSnare -= 2.0f;

      // ---- Clap rules ----
      if (f.centroidMean >= 800.0f && lowRatio < 0.4f)
      {
         if (f.microOnsets40ms >= 3)
            sClap += 5.5f;
         else if (f.microOnsets40ms == 2)
            sClap += 2.5f;

         sClap += (f.energyMid + f.energyHigh) * 2.0f;
         if (f.spectralFlatness > 0.15f)
            sClap += 2.0f;
         if (f.decayTimeMs >= 50.0f && f.decayTimeMs <= 350.0f)
            sClap += 1.0f;
      }

      // ---- HatClosed / HatOpen rules ----
      sHatClosed += highRatio * 4.0f;
      if (f.centroidMean > 4000.0f)
         sHatClosed += 2.5f;
      if (f.decayTimeMs < 80.0f)
         sHatClosed += 3.5f;
      else if (f.decayTimeMs > 120.0f)
         sHatClosed -= 2.0f;
      if (f.spectralFlatness > 0.15f)
         sHatClosed += 1.5f;
      if (f.zcr > 0.15f)
         sHatClosed += 1.5f;

      sHatOpen += highRatio * 4.0f;
      if (f.centroidMean > 4000.0f)
         sHatOpen += 2.5f;
      if (f.decayTimeMs > 140.0f)
         sHatOpen += 3.5f;
      else if (f.decayTimeMs < 90.0f)
         sHatOpen -= 2.0f;
      if (f.spectralFlatness > 0.15f)
         sHatOpen += 1.5f;
      if (f.zcr > 0.15f)
         sHatOpen += 1.5f;

      // ---- Perc rules (unpitched catch-all) ----
      sPerc += 1.2f;
      if (f.attackTimeMs < 20.0f)
         sPerc += 1.0f;
      if (f.decayTimeMs < 300.0f)
         sPerc += 1.0f;
      if (f.centroidMean >= 400.0f && f.centroidMean <= 3500.0f)
         sPerc += 1.5f;

      // ---- Piano / Synth / Tonal rules (new, pitched material) ----
      // Gated on a real pitch estimate so unpitched drums never compete here.
      const bool pitched = f.f0Hz > 60.0f && (f.f0Confidence > 0.35f || f.harmonicRatio > 0.30f);
      if (pitched)
      {
         const float pitchStrength = std::max(f.f0Confidence, f.harmonicRatio);

         sTonal += 1.5f + pitchStrength * 1.5f; // honest fallback baseline

         // Piano: fast attack, smooth exponential decay, stretched partials.
         sPiano += pitchStrength * 2.0f;
         if (f.attackTimeMs < 25.0f)
            sPiano += 2.0f;
         if (f.decayLinearityR2 > 0.7f)
            sPiano += 3.0f;
         sPiano += f.inharmonicityStretch * 4.0f;
         if (f.hasSustainPlateau)
            sPiano -= 2.5f; // a real plateau argues Synth, not Piano

         // Synth: attack not percussive, or a flat sustain plateau, stable
         // partials (low stretch).
         sSynth += pitchStrength * 1.5f;
         if (f.attackTimeMs >= 25.0f)
            sSynth += 2.0f;
         if (f.hasSustainPlateau)
            sSynth += 3.5f;
         if (f.inharmonicityStretch < 0.15f)
            sSynth += 1.0f;
         if (f.decayLinearityR2 < 0.5f)
            sSynth += 1.0f;
      }

      // 8. Filename prior (fixes D1, D2): whole-token match only, ohh/open
      // checked before hh/hat so an "OHH" file never reads as a closed hat.
      if (fileNameHint != nullptr && fileNameHint[0] != '\0')
      {
         const std::vector<std::string> tokens = Tokenize(fileNameHint);
         constexpr float kPriorBonus = 2.5f;

         if (HasAnyToken(tokens, { "kick", "kik", "bd", "bassdrum" }))
         {
            sKick += kPriorBonus;
         }
         else if (HasToken(tokens, "808"))
         {
            if (f.decayTimeMs < 250.0f)
               sKick += kPriorBonus;
            else
               sBass += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "sub", "bass" }))
         {
            sBass += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "snare", "snr", "sd", "rim" }))
         {
            sSnare += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "clap", "clp", "cp" }))
         {
            sClap += kPriorBonus;
         }
         // D1: ohh/open checked BEFORE hh/hat, so "OHH_01" (tokens: {"ohh","01"})
         // never falls into the hh/hat branch.
         else if (HasAnyToken(tokens, { "ohh", "open" }))
         {
            sHatOpen += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "hh", "hat", "chh", "closed" }))
         {
            sHatClosed += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "piano", "keys", "rhodes", "ep" }))
         {
            sPiano += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "synth", "pad", "lead", "pluck", "stab" }))
         {
            sSynth += kPriorBonus;
         }
         else if (HasAnyToken(tokens, { "perc", "tom", "shaker", "conga", "cow" }))
         {
            sPerc += kPriorBonus;
         }
      }

      res.scores[(int)DrumClass::Kick] = std::max(0.0f, sKick);
      res.scores[(int)DrumClass::Bass] = std::max(0.0f, sBass);
      res.scores[(int)DrumClass::Snare] = std::max(0.0f, sSnare);
      res.scores[(int)DrumClass::Clap] = std::max(0.0f, sClap);
      res.scores[(int)DrumClass::HatClosed] = std::max(0.0f, sHatClosed);
      res.scores[(int)DrumClass::HatOpen] = std::max(0.0f, sHatOpen);
      res.scores[(int)DrumClass::Perc] = std::max(0.0f, sPerc);
      res.scores[(int)DrumClass::Synth] = std::max(0.0f, sSynth);
      res.scores[(int)DrumClass::Piano] = std::max(0.0f, sPiano);
      res.scores[(int)DrumClass::Tonal] = std::max(0.0f, sTonal);

      float sumScores = 0.0f;
      for (int i = 0; i < kNumClasses; i++)
         sumScores += res.scores[i];

      if (sumScores > 1e-6f)
      {
         for (int i = 0; i < kNumClasses; i++)
            res.scores[i] /= sumScores;
      }
      else
      {
         res.scores[(int)DrumClass::Perc] = 1.0f;
      }

      float bestScore = -1.0f, secondScore = -1.0f;
      int bestIdx = (int)DrumClass::Perc;
      for (int i = 0; i < kNumClasses; i++)
      {
         const float sc = res.scores[i];
         if (sc > bestScore)
         {
            secondScore = bestScore;
            bestScore = sc;
            bestIdx = i;
         }
         else if (sc > secondScore)
         {
            secondScore = sc;
         }
      }

      res.cls = (DrumClass)bestIdx;
      res.confidence = std::max(0.0f, bestScore - std::max(0.0f, secondScore));

      return res;
   }
}
