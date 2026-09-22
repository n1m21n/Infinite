#include "DrumClassifier.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

   static std::string ToLower(const std::string& str)
   {
      std::string s = str;
      for (char& c : s)
         c = (char)std::tolower((unsigned char)c);
      return s;
   }

   static bool ContainsToken(const std::string& s, const char* token)
   {
      return s.find(token) != std::string::npos;
   }

   Result Classify(const float* mono, int len, double sr, const char* fileNameHint)
   {
      Result res;
      if (mono == nullptr || len <= 0 || sr <= 1000.0)
         return res;

      Features& f = res.f;

      // 1. Envelope analysis: peak, attack time, decay time
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
         return res; // silent

      // Attack: 10% to 90% peak
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

      // Decay: peak to -20 dB (0.10 * peakVal)
      // Smoothed peak envelope tracking backwards/forwards to avoid local zero-crossings
      const int smoothWin = std::max(1, (int)(sr * 0.003)); // 3 ms smoothing window
      int tDrop = len;
      for (int i = peakIdx; i < len; i += smoothWin)
      {
         float localMax = 0.0f;
         const int end = std::min(len, i + smoothWin);
         for (int j = i; j < end; j++)
            localMax = std::max(localMax, std::fabs(mono[j]));

         if (localMax < v10)
         {
            tDrop = i;
            break;
         }
      }
      f.decayTimeMs = (float)((tDrop - peakIdx) * 1000.0 / sr);

      // 2. Zero-crossing rate
      int zcrCount = 0;
      for (int i = 0; i < len - 1; i++)
      {
         if ((mono[i] >= 0.0f && mono[i + 1] < 0.0f) ||
             (mono[i] < 0.0f && mono[i + 1] >= 0.0f))
         {
            zcrCount++;
         }
      }
      f.zcr = (float)zcrCount / (float)std::max(1, len - 1);

      // 3. Micro-onset count in first 40 ms
      // Clap signature: 3-4 bursts separated by 5-15 ms. Snare has 1.
      const int n40 = std::min(len, (int)(sr * 0.040));
      const int hopMicro = 128; // ~2.9 ms at 44.1k
      const int numMicroFrames = n40 / hopMicro;
      if (numMicroFrames >= 2)
      {
         std::vector<float> energy(numMicroFrames, 0.0f);
         for (int m = 0; m < numMicroFrames; m++)
         {
            float sumSq = 0.0f;
            const int base = m * hopMicro;
            for (int j = 0; j < hopMicro; j++)
               sumSq += mono[base + j] * mono[base + j];
            energy[m] = sumSq;
         }

         std::vector<float> flux(numMicroFrames, 0.0f);
         float maxFlux = 0.0f;
         for (int m = 1; m < numMicroFrames; m++)
         {
            flux[m] = std::max(0.0f, energy[m] - energy[m - 1]);
            maxFlux = std::max(maxFlux, flux[m]);
         }

         int microCount = 0;
         int lastPeakFrame = -100;
         const float thresh = 0.12f * maxFlux;
         for (int m = 1; m < numMicroFrames - 1; m++)
         {
            if (flux[m] > thresh && flux[m] >= flux[m - 1] && flux[m] >= flux[m + 1])
            {
               // Check spacing from last peak (at least ~4 ms = ~1.5 hops)
               if (m - lastPeakFrame >= 2)
               {
                  microCount++;
                  lastPeakFrame = m;
               }
            }
         }
         f.microOnsets40ms = std::max(1, microCount);
      }
      else
      {
         f.microOnsets40ms = 1;
      }

      // 4. Low-band pitch stability (30 - 250 Hz autocorrelation)
      const int minLag = (int)std::floor(sr / 250.0);
      const int maxLag = (int)std::ceil(sr / 30.0);
      const int corrLen = std::min(len - maxLag, (int)(sr * 0.150));
      if (corrLen > maxLag && maxLag > minLag)
      {
         auto getPeakCorr = [&](int offset) -> std::pair<int, float>
         {
            float maxR = 0.0f;
            int bestLag = 0;
            float e0 = 0.0f;
            for (int i = 0; i < corrLen; i++)
               e0 += mono[offset + i] * mono[offset + i];
            if (e0 < 1e-6f)
               return { 0, 0.0f };

            for (int lag = minLag; lag <= maxLag; lag++)
            {
               float cross = 0.0f;
               float eLag = 0.0f;
               for (int i = 0; i < corrLen; i++)
               {
                  cross += mono[offset + i] * mono[offset + i + lag];
                  eLag += mono[offset + i + lag] * mono[offset + i + lag];
               }
               const float denom = std::sqrt(e0 * eLag);
               const float normR = denom > 1e-6f ? cross / denom : 0.0f;
               if (normR > maxR)
               {
                  maxR = normR;
                  bestLag = lag;
               }
            }
            return { bestLag, maxR };
         };

         auto c1 = getPeakCorr(0);
         float stability = 0.0f;
         if (len >= corrLen * 2 + maxLag)
         {
            auto c2 = getPeakCorr(corrLen / 2);
            if (c1.second > 0.5f && c2.second > 0.5f && std::abs(c1.first - c2.first) <= 4)
               stability = 0.5f * (c1.second + c2.second);
            else if (c1.second > 0.5f)
               stability = 0.5f * c1.second;
         }
         else
         {
            stability = c1.second > 0.5f ? c1.second : 0.0f;
         }
         f.lowBandPitchStability = std::clamp(stability, 0.0f, 1.0f);
      }

      // 5. STFT feature extraction (1024-pt Hann STFT over first ~250 ms)
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
         std::vector<float> centroids(numFrames, 0.0f);

         float totalEnergy = 0.0f;
         float bandSub = 0.0f;     // 20-90 Hz
         float bandLow = 0.0f;     // 90-250 Hz
         float bandLowMid = 0.0f;  // 250-1000 Hz
         float bandMid = 0.0f;     // 1000-4000 Hz
         float bandHigh = 0.0f;    // 4000-10000 Hz
         float bandAir = 0.0f;     // >10000 Hz

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

            for (int k = 1; k < numBins; k++)
            {
               const float m = std::sqrt(re[k] * re[k] + im[k] * im[k]) * magScale;
               mag[k] = m;
               const float p = m * m;
               avgPower[k] += p;

               const float fHz = k * binHz;
               numC += fHz * m;
               denC += m;

               if (fHz >= 20.0f && fHz < 90.0f)        bandSub += p;
               else if (fHz >= 90.0f && fHz < 250.0f)   bandLow += p;
               else if (fHz >= 250.0f && fHz < 1000.0f) bandLowMid += p;
               else if (fHz >= 1000.0f && fHz < 4000.0f) bandMid += p;
               else if (fHz >= 4000.0f && fHz < 10000.0f) bandHigh += p;
               else if (fHz >= 10000.0f)                bandAir += p;

               totalEnergy += p;
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

         // Centroid trajectory over first ~50 ms (first ~8 frames)
         const int frames50ms = std::min(numFrames, (int)(sr * 0.050 / kHop));
         if (frames50ms >= 3)
         {
            const float cStart = centroids[0];
            const float cEnd = centroids[frames50ms - 1];
            f.centroidTrajectory = cEnd - cStart;
         }

         if (totalEnergy > 1e-9f)
         {
            f.energySub = bandSub / totalEnergy;
            f.energyLow = bandLow / totalEnergy;
            f.energyLowMid = bandLowMid / totalEnergy;
            f.energyMid = bandMid / totalEnergy;
            f.energyHigh = bandHigh / totalEnergy;
            f.energyAir = bandAir / totalEnergy;
         }

         // Spectral Flatness (Wiener entropy: geometric mean / arithmetic mean of power)
         double logSum = 0.0;
         double linSum = 0.0;
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
      }

      // 6. Decision rule: weighted scores per class
      float sKick = 0.0f;
      float sBass = 0.0f;
      float sSnare = 0.0f;
      float sClap = 0.0f;
      float sHatClosed = 0.0f;
      float sHatOpen = 0.0f;
      float sPerc = 0.0f;

      const float lowRatio = f.energySub + f.energyLow;
      const float highRatio = f.energyHigh + f.energyAir;

      // ---- Kick rules ----
      sKick += lowRatio * 4.0f;
      if (f.centroidMean < 300.0f)
         sKick += 2.5f;
      else if (f.centroidMean < 600.0f)
         sKick += 1.0f;

      if (f.centroidTrajectory < -5.0f)
         sKick += 2.0f; // pitch dropping

      if (f.decayTimeMs < 250.0f)
         sKick += 2.0f;
      else if (f.decayTimeMs > 400.0f)
         sKick -= 2.0f; // too sustained for typical kick

      if (f.spectralFlatness < 0.05f)
         sKick += 1.5f; // tonal

      if (f.microOnsets40ms > 1 && f.centroidMean > 800.0f)
         sKick -= 2.0f;

      // ---- Bass rules ----
      sBass += lowRatio * 3.5f;
      if (f.decayTimeMs > 400.0f)
         sBass += 3.5f; // sustained body
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
         sSnare -= 2.0f; // multiple bursts means clap

      // ---- Clap rules ----
      // Claps are broadband mid/high bursts with micro-onsets; never sub/bass
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

      // ---- HatClosed rules ----
      sHatClosed += highRatio * 4.0f;
      if (f.centroidMean > 4000.0f)
         sHatClosed += 2.5f;
      if (f.decayTimeMs < 80.0f)
         sHatClosed += 3.5f;
      else if (f.decayTimeMs > 120.0f)
         sHatClosed -= 2.0f; // too long for closed hat
      if (f.spectralFlatness > 0.15f)
         sHatClosed += 1.5f;
      if (f.zcr > 0.15f)
         sHatClosed += 1.5f;

      // ---- HatOpen rules ----
      sHatOpen += highRatio * 4.0f;
      if (f.centroidMean > 4000.0f)
         sHatOpen += 2.5f;
      if (f.decayTimeMs > 140.0f)
         sHatOpen += 3.5f;
      else if (f.decayTimeMs < 90.0f)
         sHatOpen -= 2.0f; // too short for open hat
      if (f.spectralFlatness > 0.15f)
         sHatOpen += 1.5f;
      if (f.zcr > 0.15f)
         sHatOpen += 1.5f;

      // ---- Perc rules ----
      sPerc += 1.2f; // baseline catch-all
      if (f.attackTimeMs < 20.0f)
         sPerc += 1.0f;
      if (f.decayTimeMs < 300.0f)
         sPerc += 1.0f;
      if (f.centroidMean >= 400.0f && f.centroidMean <= 3500.0f)
         sPerc += 1.5f;

      // 7. Filename prior
      if (fileNameHint != nullptr && fileNameHint[0] != '\0')
      {
         const std::string name = ToLower(fileNameHint);
         constexpr float kPriorBonus = 2.5f;

         if (ContainsToken(name, "kick") || ContainsToken(name, "kik") ||
             ContainsToken(name, "bd") || ContainsToken(name, "bassdrum"))
         {
            sKick += kPriorBonus;
         }
         else if (ContainsToken(name, "808"))
         {
            if (f.decayTimeMs < 250.0f)
               sKick += kPriorBonus;
            else
               sBass += kPriorBonus;
         }
         else if (ContainsToken(name, "sub") || ContainsToken(name, "bass"))
         {
            sBass += kPriorBonus;
         }
         else if (ContainsToken(name, "snare") || ContainsToken(name, "snr") ||
                  ContainsToken(name, "sd") || ContainsToken(name, "rim"))
         {
            sSnare += kPriorBonus;
         }
         else if (ContainsToken(name, "clap") || ContainsToken(name, "clp") || ContainsToken(name, "cp"))
         {
            sClap += kPriorBonus;
         }
         else if (ContainsToken(name, "hh") || ContainsToken(name, "hat") ||
                  ContainsToken(name, "chh") || ContainsToken(name, "closed"))
         {
            sHatClosed += kPriorBonus;
         }
         else if (ContainsToken(name, "ohh") || ContainsToken(name, "open"))
         {
            sHatOpen += kPriorBonus;
         }
         else if (ContainsToken(name, "perc") || ContainsToken(name, "tom") ||
                  ContainsToken(name, "shaker") || ContainsToken(name, "conga") ||
                  ContainsToken(name, "cow"))
         {
            sPerc += kPriorBonus;
         }
      }

      // Clamp raw scores >= 0
      res.scores[(int)DrumClass::Kick] = std::max(0.0f, sKick);
      res.scores[(int)DrumClass::Bass] = std::max(0.0f, sBass);
      res.scores[(int)DrumClass::Snare] = std::max(0.0f, sSnare);
      res.scores[(int)DrumClass::Clap] = std::max(0.0f, sClap);
      res.scores[(int)DrumClass::HatClosed] = std::max(0.0f, sHatClosed);
      res.scores[(int)DrumClass::HatOpen] = std::max(0.0f, sHatOpen);
      res.scores[(int)DrumClass::Perc] = std::max(0.0f, sPerc);

      // Normalize scores
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

      // Find top two classes
      float bestScore = -1.0f;
      float secondScore = -1.0f;
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
