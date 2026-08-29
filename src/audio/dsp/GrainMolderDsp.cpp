#include "GrainMolderDsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include "audio/dsp/PortableFft.h"

namespace GrainMolderDsp
{
   void Process(const float* mono, int len, double sr, const Params& p,
                std::vector<float>& outL, std::vector<float>* outR,
                const float* rightIn,
                const std::atomic<bool>* abort)
   {
      outL.assign(len, 0.0f);
      if (outR)
         outR->assign(len, 0.0f);

      if (!mono || len <= 0 || sr <= 0.0)
         return;

      const int grainLen = std::max(4, (int)(p.grainMs * sr / 1000.0));
      const int hop = std::max(2, grainLen / 2);

      // If sample is too short to form multiple overlapping grains, copy through
      if (len < 2 * grainLen)
      {
         std::copy(mono, mono + len, outL.begin());
         if (outR && rightIn)
            std::copy(rightIn, rightIn + len, outR->begin());
         else if (outR)
            std::copy(mono, mono + len, outR->begin());
         return;
      }

      int K = (len - grainLen) / hop + 1;
      K = std::clamp(K, 2, 8192);

      // Analysis / synthesis Hann window
      std::vector<float> window(grainLen);
      for (int n = 0; n < grainLen; n++)
         window[n] = 0.5f * (1.0f - cosf((float)(2.0 * M_PI * (double)n / (double)grainLen)));

      // Prepare FFT if spectral centroid key is requested
      PortableFft::RealFft fft;
      int fftLog2 = 0;
      int fftSize = 0;
      std::vector<float> fftTimeBuf;
      std::vector<float> fftReal;
      std::vector<float> fftImag;

      if (p.key == 1) // Brightness
      {
         fftSize = 1;
         fftLog2 = 0;
         while (fftSize < grainLen)
         {
            fftSize <<= 1;
            fftLog2++;
         }
         fft.Prepare(fftLog2);
         fftTimeBuf.assign(fftSize, 0.0f);
         fftReal.assign(fftSize / 2, 0.0f);
         fftImag.assign(fftSize / 2, 0.0f);
      }

      // Compute metric per grain
      std::vector<float> metrics(K, 0.0f);
      for (int g = 0; g < K; g++)
      {
         if (abort && abort->load(std::memory_order_relaxed))
            return;

         const int startIdx = g * hop;
         const int available = std::min(grainLen, len - startIdx);

         if (p.key == 0) // Level (RMS)
         {
            double sumSq = 0.0;
            for (int i = 0; i < available; i++)
            {
               const float s = mono[startIdx + i];
               sumSq += (double)s * (double)s;
            }
            metrics[g] = (float)sqrt(sumSq / (double)grainLen);
         }
         else if (p.key == 1) // Brightness (Spectral Centroid)
         {
            std::fill(fftTimeBuf.begin(), fftTimeBuf.end(), 0.0f);
            for (int i = 0; i < available; i++)
               fftTimeBuf[i] = mono[startIdx + i] * window[i];

            fft.Forward(fftTimeBuf.data(), fftLog2, fftReal.data(), fftImag.data());

            // Halve forward spectrum for standard DFT scaling
            for (int k = 0; k < fftSize / 2; k++)
            {
               fftReal[k] *= 0.5f;
               fftImag[k] *= 0.5f;
            }

            double num = 0.0;
            double den = 0.0;

            // DC
            const double mag0 = (double)std::fabs(fftReal[0]);
            den += mag0;

            // Nyquist
            const double magNyq = (double)std::fabs(fftImag[0]);
            const double freqNyq = (double)(fftSize / 2) * (sr / (double)fftSize);
            num += freqNyq * magNyq;
            den += magNyq;

            // Complex bins
            for (int k = 1; k < fftSize / 2; k++)
            {
               const double r = fftReal[k];
               const double im = fftImag[k];
               const double mag = sqrt(r * r + im * im);
               const double freq = (double)k * (sr / (double)fftSize);
               num += freq * mag;
               den += mag;
            }

            metrics[g] = (den > 1e-9) ? (float)(num / den) : 0.0f;
         }
         else // Random (seeded xorshift)
         {
            uint32_t rng = p.seed ^ (uint32_t)(g * 0x9e3779b9u);
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            metrics[g] = ((float)(rng & 0x00FFFFFFu) / 16777216.0f);
         }
      }

      // Rank grains by metric
      std::vector<int> sortedIndices(K);
      std::iota(sortedIndices.begin(), sortedIndices.end(), 0);

      if (p.descending)
      {
         std::stable_sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
            return metrics[a] > metrics[b];
         });
      }
      else
      {
         std::stable_sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
            return metrics[a] < metrics[b];
         });
      }

      std::vector<int> rank(K);
      for (int r = 0; r < K; r++)
         rank[sortedIndices[r]] = r;

      // Blend original position against sorted rank
      const float amt = std::clamp(p.amount, 0.0f, 1.0f);
      std::vector<float> sortKey(K);
      for (int g = 0; g < K; g++)
         sortKey[g] = (1.0f - amt) * (float)g + amt * (float)rank[g];

      // Final permutation of grain indices for output slots 0..K-1
      std::vector<int> perm(K);
      std::iota(perm.begin(), perm.end(), 0);
      std::stable_sort(perm.begin(), perm.end(), [&](int a, int b) {
         return sortKey[a] < sortKey[b];
      });

      // Resynthesis via Overlap-Add
      std::vector<float> olaNorm(len, 0.0f);

      for (int j = 0; j < K; j++)
      {
         if (abort && abort->load(std::memory_order_relaxed))
            return;

         const int srcG = perm[j];
         const int srcPos = srcG * hop;
         const int dstPos = j * hop;

         const int nSamples = std::min({ grainLen, len - srcPos, len - dstPos });
         for (int n = 0; n < nSamples; n++)
         {
            const float w = window[n];
            const float w2 = w * w;
            outL[dstPos + n] += mono[srcPos + n] * w2;
            if (outR && rightIn)
               (*outR)[dstPos + n] += rightIn[srcPos + n] * w2;
            else if (outR)
               (*outR)[dstPos + n] += mono[srcPos + n] * w2;

            olaNorm[dstPos + n] += w2;
         }
      }

      // Normalise by OLA window sum
      for (int i = 0; i < len; i++)
      {
         const float norm = olaNorm[i];
         if (norm > 1e-6f)
         {
            outL[i] /= norm;
            if (outR)
               (*outR)[i] /= norm;
         }
      }

      // Any remaining tail past K*hop is filled from original tail if unfilled
      for (int i = (K - 1) * hop + grainLen; i < len; i++)
      {
         if (olaNorm[i] <= 1e-6f)
         {
            outL[i] = mono[i];
            if (outR && rightIn)
               (*outR)[i] = rightIn[i];
            else if (outR)
               (*outR)[i] = mono[i];
         }
      }

      // Peak normalise only if result exceeds 0 dBFS (> 1.0)
      float maxPeak = 0.0f;
      for (int i = 0; i < len; i++)
      {
         maxPeak = std::max(maxPeak, std::fabs(outL[i]));
         if (outR)
            maxPeak = std::max(maxPeak, std::fabs((*outR)[i]));
      }

      if (maxPeak > 1.0f)
      {
         const float scale = 1.0f / maxPeak;
         for (int i = 0; i < len; i++)
         {
            outL[i] *= scale;
            if (outR)
               (*outR)[i] *= scale;
         }
      }
   }
}
