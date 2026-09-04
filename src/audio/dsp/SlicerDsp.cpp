#include "SlicerDsp.h"

#include <algorithm>
#include <cmath>

#include "PortableFft.h"

namespace
{
   // Log-magnitude compression constant. log(1 + c*|X|) with c = 1000 is the
   // standard figure for the compressed spectral-flux ODF (Dixon, DAFx-06
   // §2.1's "logarithmic" variant); it keeps quiet transients visible without
   // letting broadband noise dominate the sum.
   constexpr float kCompression = 1000.0f;

   // Peak-picker window, in ODF frames (Dixon, DAFx-06 §3): a frame must be
   // the maximum over [n-w, n+w] and exceed the mean over [n-mw, n+w] by
   // delta. m = 3 there; w = 3 frames is ~17 ms at the ~172 Hz ODF rate.
   constexpr int kPeakW = 3;
   constexpr int kPeakMultiplier = 3;

   inline int Log2Int(int n)
   {
      int r = 0;
      while ((1 << r) < n)
         r++;
      return r;
   }
}

namespace SlicerDsp
{
   void Detect(const float* mono, int len, double sr, const Params& p,
               std::vector<int>& outOnsets, std::vector<float>& outStrengths,
               const std::atomic<bool>* abort)
   {
      outOnsets.clear();
      outStrengths.clear();
      if (mono == nullptr || len <= 0 || sr <= 0.0)
         return;

      // Rule 5 of the post-processing order: there is always a slice at t=0.
      // Emitted first so an abort (or a file shorter than one analysis frame)
      // still yields a usable single-slice result.
      outOnsets.push_back(0);
      outStrengths.push_back(kForcedOnsetStrength);

      const int fftSize = std::max(64, p.fftSize);
      const int log2N = Log2Int(fftSize);
      if ((1 << log2N) != fftSize)
         return; // caller passed a non-power-of-two; refuse rather than guess
      if (len < fftSize * 2)
         return; // too short to say anything about; the t=0 slice is the answer

      // Hop scales with sample rate so the ODF frame rate stays ~172 Hz
      // regardless of the file's rate; the FFT size stays a power of two.
      const int hop = std::max(1, (int)std::lround(256.0 * sr / 44100.0));
      // Frames are CENTRED, i.e. analysis frame n is the window of `mono`
      // centred on sample n*hop, zero-padded where it runs off either end.
      // This is the usual STFT convention (Portnoff/Dolson) and it is what
      // makes the ODF frame index directly convertible to a sample position:
      // an un-centred framing reports the onset up to fftSize/2 samples EARLY,
      // which lands the slice in the silence ahead of the attack, where the
      // silence gate below then rejects it outright.
      const int numFrames = len / hop + 1;
      if (numFrames < 2 * kPeakW + 2)
         return;
      const int halfN = fftSize / 2;

      PortableFft::RealFft fft;
      if (!fft.Prepare(log2N))
         return;

      std::vector<float> window(fftSize);
      PortableFft::HannWindowNorm(window.data(), fftSize);

      const int numBins = fftSize / 2;
      const float magScale = 2.0f / (float)fftSize;

      std::vector<float> frame(fftSize);
      std::vector<float> re(numBins), im(numBins);
      std::vector<float> mag(numBins, 0.0f);
      std::vector<float> prevMag(numBins, 0.0f);
      std::vector<float> odf(numFrames, 0.0f);

      for (int n = 0; n < numFrames; n++)
      {
         if (abort != nullptr && abort->load(std::memory_order_relaxed))
            return;

         const int base = n * hop - halfN;
         for (int i = 0; i < fftSize; i++)
         {
            const int src = base + i;
            frame[i] = (src >= 0 && src < len) ? mono[src] * window[i] : 0.0f;
         }

         fft.Forward(frame.data(), log2N, re.data(), im.data());

         // PortableFft mirrors vDSP_fft_zrip: bins come back x2-scaled and
         // bin 0 packs DC in the real array and Nyquist in the imaginary one.
         mag[0] = std::fabs(re[0]) * magScale;
         for (int k = 1; k < numBins; k++)
            mag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]) * magScale;

         if (n > 0)
         {
            float flux = 0.0f;
            for (int k = 0; k < numBins; k++)
            {
               // SuperFlux: the reference frame is the 3-bin maximum filter
               // of the previous frame's magnitudes, so a partial drifting
               // by up to one bin (vibrato, a glide) does not register as a
               // rise.
               float ref = prevMag[k];
               if (k > 0)
                  ref = std::max(ref, prevMag[k - 1]);
               if (k + 1 < numBins)
                  ref = std::max(ref, prevMag[k + 1]);

               const float d = std::log(1.0f + kCompression * mag[k]) -
                               std::log(1.0f + kCompression * ref);
               if (d > 0.0f)
                  flux += d;
            }
            odf[n] = flux;
         }

         std::swap(mag, prevMag);
      }

      // Normalise the ODF to zero mean / unit standard deviation over the
      // whole file, so `delta` below is a file-independent number.
      {
         double sum = 0.0;
         for (int n = 0; n < numFrames; n++)
            sum += odf[n];
         const double mean = sum / (double)numFrames;
         double var = 0.0;
         for (int n = 0; n < numFrames; n++)
         {
            const double d = odf[n] - mean;
            var += d * d;
         }
         const double sd = std::sqrt(var / (double)numFrames);
         const float inv = (sd > 1e-9) ? (float)(1.0 / sd) : 1.0f;
         for (int n = 0; n < numFrames; n++)
            odf[n] = (float)((odf[n] - mean) * inv);
      }

      // sensitivity 0 -> delta_max (least sensitive), 100 -> delta_min.
      constexpr float kDeltaMin = 0.02f;
      constexpr float kDeltaMax = 1.0f;
      const float sens = std::clamp(p.sensitivity, 0.0f, 100.0f);
      const float delta = kDeltaMax * std::pow(kDeltaMin / kDeltaMax, sens / 100.0f);

      // Short-term energy envelope, used both by the silence gate and by the
      // backtracking step. One value per `energyHop` samples.
      const int energyHop = 64;
      const int energyWin = 128;
      const int numEnergy = std::max(1, (len - energyWin) / energyHop + 1);
      std::vector<float> energy((size_t)numEnergy, 0.0f);
      for (int e = 0; e < numEnergy; e++)
      {
         const int b = e * energyHop;
         double acc = 0.0;
         for (int i = 0; i < energyWin; i++)
            acc += (double)mono[b + i] * mono[b + i];
         energy[e] = (float)std::sqrt(acc / (double)energyWin);
      }
      const float silenceGateLin = std::pow(10.0f, p.silenceGateDb / 20.0f);

      struct Peak
      {
         int frame;
         float strength;
      };
      std::vector<Peak> peaks;

      for (int n = kPeakW; n < numFrames - kPeakW; n++)
      {
         if (abort != nullptr && abort->load(std::memory_order_relaxed))
            break;

         const float f = odf[n];
         bool isMax = true;
         for (int k = n - kPeakW; k <= n + kPeakW; k++)
         {
            if (odf[k] > f)
            {
               isMax = false;
               break;
            }
         }
         if (!isMax)
            continue;

         const int lo = std::max(0, n - kPeakMultiplier * kPeakW);
         const int hi = std::min(numFrames - 1, n + kPeakW);
         double acc = 0.0;
         for (int k = lo; k <= hi; k++)
            acc += odf[k];
         const float localMean = (float)(acc / (double)(hi - lo + 1));
         if (f < localMean + delta)
            continue;

         peaks.push_back({ n, f });
      }

      if (peaks.empty())
         return;

      // 1. Minimum inter-onset interval - keep the stronger of a colliding
      //    pair. Peaks arrive in ascending time order already.
      const int minIoiSamples = std::max(1, (int)std::lround(p.minIoiSeconds * sr));
      {
         std::vector<Peak> kept;
         kept.reserve(peaks.size());
         for (const Peak& pk : peaks)
         {
            const int posSamples = pk.frame * hop;
            if (!kept.empty())
            {
               const int prevPos = kept.back().frame * hop;
               if (posSamples - prevPos < minIoiSamples)
               {
                  if (pk.strength > kept.back().strength)
                     kept.back() = pk;
                  continue;
               }
            }
            kept.push_back(pk);
         }
         peaks.swap(kept);
      }

      struct Onset
      {
         int sample;
         float strength;
      };
      std::vector<Onset> onsets;
      onsets.reserve(peaks.size());

      const int backtrackSamples = std::max(1, (int)std::lround(0.015 * sr));
      const int zeroSnapSamples = std::max(1, (int)std::lround(0.001 * sr));

      for (const Peak& pk : peaks)
      {
         int pos = pk.frame * hop;
         pos = std::clamp(pos, 0, len - 1);

         // 2. Silence gate: reject onsets whose surrounding energy sits below
         //    the gate. Reading the envelope rather than the raw samples so a
         //    single loud sample cannot open the gate on its own.
         {
            // Biased forward on purpose: an onset is defined by the energy
            // that FOLLOWS it, and the ODF frame can sit a fraction of a hop
            // ahead of the physical attack.
            const int e = std::clamp(pos / energyHop, 0, numEnergy - 1);
            const int eLo = std::max(0, e - 2);
            const int eHi = std::min(numEnergy - 1, e + (int)std::lround(0.010 * sr / energyHop));
            float peakEnergy = 0.0f;
            for (int k = eLo; k <= eHi; k++)
               peakEnergy = std::max(peakEnergy, energy[k]);
            if (peakEnergy < silenceGateLin)
               continue;
         }

         // 3. Backtrack to the nearest preceding local minimum of short-term
         //    energy within 15 ms. Without this a kick's slice starts after
         //    the attack has already begun and loses its thump.
         {
            const int eNow = std::clamp(pos / energyHop, 0, numEnergy - 1);
            const int eLimit = std::max(0, (pos - backtrackSamples) / energyHop);
            int bestE = eNow;
            // Walk back to the FIRST local minimum encountered, not the
            // smallest value in the whole window: in the silence ahead of a
            // burst every value is equally small, and taking the global
            // minimum would push the slice the full 15 ms early instead of
            // landing it right on the foot of the attack.
            for (int k = eNow - 1; k >= eLimit; k--)
            {
               const bool notRisingBack = energy[k] <= energy[k + 1];
               const bool atFloor = (k == eLimit) || (energy[k] <= energy[k - 1]);
               if (notRisingBack && atFloor)
               {
                  bestE = k;
                  break;
               }
            }
            pos = std::clamp(bestE * energyHop, 0, len - 1);
         }

         // 4. Snap to the nearest zero crossing within 1 ms, so a slice never
         //    starts on a DC step (which clicks even through the fade-in).
         {
            int best = pos;
            int bestDist = zeroSnapSamples + 1;
            const int lo = std::max(1, pos - zeroSnapSamples);
            const int hi = std::min(len - 1, pos + zeroSnapSamples);
            for (int i = lo; i <= hi; i++)
            {
               const bool crossing = (mono[i - 1] <= 0.0f && mono[i] >= 0.0f) ||
                                     (mono[i - 1] >= 0.0f && mono[i] <= 0.0f);
               if (!crossing)
                  continue;
               const int d = std::abs(i - pos);
               if (d < bestDist)
               {
                  bestDist = d;
                  best = i;
               }
            }
            pos = best;
         }

         if (pos <= 0)
            continue; // the forced onset at 0 already covers this
         onsets.push_back({ pos, pk.strength });
      }

      // The min-IOI rule has to hold against the forced onset at 0 too, and
      // backtracking can have moved a peak backwards into it.
      while (!onsets.empty() && onsets.front().sample < minIoiSamples)
         onsets.erase(onsets.begin());

      // Backtracking can also collapse two peaks onto the same local minimum;
      // re-enforce ascending strict order and the min IOI.
      {
         std::vector<Onset> kept;
         kept.reserve(onsets.size());
         for (const Onset& o : onsets)
         {
            if (!kept.empty() && o.sample - kept.back().sample < minIoiSamples)
            {
               if (o.strength > kept.back().strength)
                  kept.back().strength = o.strength;
               continue;
            }
            kept.push_back(o);
         }
         onsets.swap(kept);
      }

      // 6. Hard cap: keep the strongest, then re-sort by time. -1 because the
      //    forced onset at 0 already occupies one slot.
      const int cap = std::max(1, p.maxSlices) - 1;
      if ((int)onsets.size() > cap)
      {
         std::stable_sort(onsets.begin(), onsets.end(),
                          [](const Onset& a, const Onset& b) { return a.strength > b.strength; });
         onsets.resize((size_t)cap);
         std::stable_sort(onsets.begin(), onsets.end(),
                          [](const Onset& a, const Onset& b) { return a.sample < b.sample; });
      }

      for (const Onset& o : onsets)
      {
         outOnsets.push_back(o.sample);
         outStrengths.push_back(o.strength);
      }
   }
}
