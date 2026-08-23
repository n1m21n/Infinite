#pragma once

#include <cmath>
#include <cstring>
#include <vector>

// Portable stand-in for the subset of vDSP's real FFT API the two nodes that
// use it depend on (AudioFilePlayerAudioNode in AnalyzeNodes.cpp and
// AudioPaulStretchNode in PaulStretchNode.cpp). Iterative radix-2 complex
// FFT with a real-signal wrapper.
//
// Deliberately mirrors vDSP_fft_zrip's conventions so the calling code keeps
// its existing de-interleaving (*0.5) and normalization factors unchanged:
//
//   Forward:  N real samples -> N/2-bin split spectrum, scaled x2 versus the
//             mathematical DFT. Bin 0 holds 2*Re(X_0) (DC), bin index 0 of
//             the IMAG array holds 2*Re(X_N/2) (Nyquist) - same packing zrip
//             uses.
//
//   Inverse:  matches vDSP_fft_zrip's FFT_INVERSE exactly, which is what the
//             shared (unfenced) normalisation in PaulStretchNode assumes -
//             it applies normScale = 1/(N*1.5) to whichever backend ran, so
//             the two must agree bit-for-bit in scaling, not just in shape.
//             vDSP's inverse is UNNORMALISED: on a standard-DFT-scaled
//             packed spectrum (PaulStretch's paths all halve Forward's x2
//             before reconstructing) it yields N*x, not x. So there is no
//             1/N here on purpose. Forward-then-inverse round-trips to N*x.
//
// Tables are prepared once for a maximum size (main thread, construction);
// runs at smaller power-of-two sizes reuse them via stride indexing, which
// is what PaulStretch needs when its window size changes at runtime.

namespace PortableFft
{
   class RealFft
   {
   public:
      ~RealFft() = default;

      bool Prepare(int maxLog2Size)
      {
         if (maxLog2Size < 2 || maxLog2Size > 20)
            return false;
         mMaxLog2 = maxLog2Size;
         const int maxN = 1 << mMaxLog2;

         // Bit-reversal permutation for the maximum size (smaller sizes use
         // the low bits).
         mRev.resize(maxN);
         int bits = mMaxLog2;
         for (int i = 0; i < maxN; i++)
         {
            unsigned r = 0;
            for (int b = 0; b < bits; b++)
               if (i & (1 << b))
                  r |= 1u << (bits - 1 - b);
            mRev[i] = r;
         }

         // Twiddle factors for the maximum size; a run at size N reads every
         // maxN/N-th entry.
         mCos.resize(maxN / 2);
         mSin.resize(maxN / 2);
         for (int i = 0; i < maxN / 2; i++)
         {
            const double a = -2.0 * 3.14159265358979323846 * (double)i / (double)maxN;
            mCos[i] = (float)std::cos(a);
            mSin[i] = (float)std::sin(a);
         }

         mRe.resize(maxN);
         mIm.resize(maxN);
         // Inverse() needs somewhere to assemble its mirrored spectrum that
         // is NOT mRe/mIm: RunCore's first loop does mRe[i] = inReal[r]
         // through a non-identity permutation, so handing it mRe as input
         // corrupts the data as it goes. Sized here so the class stays
         // allocation-free after Prepare() - Inverse() runs on the audio
         // thread via AudioPaulStretchNode::ProcessBlock.
         mStageRe.resize(maxN);
         mStageIm.resize(maxN);
         return true;
      }

      int MaxSize() const { return 1 << mMaxLog2; }

      // `samples` has N real values; outReal/outImag have N/2 each and receive
      // the x2-scaled split spectrum (see class comment).
      void Forward(const float* samples, int log2N, float* outReal, float* outImag)
      {
         const int n = 1 << log2N;
         RunCore(samples, nullptr, log2N, false);

         // Unscramble the packed-real convention:
         //   outReal[0]     = 2*DC          (= 2*X_0.re)
         //   outImag[0]     = 2*Nyquist     (= 2*X_n/2.re)
         //   outReal[k]/outImag[k] = 2*X_k.re/im
         outReal[0] = mRe[0] * 2.0f;
         outImag[0] = mRe[n / 2] * 2.0f;
         for (int k = 1; k < n / 2; k++)
         {
            outReal[k] = mRe[k] * 2.0f;
            outImag[k] = mIm[k] * 2.0f;
         }
      }

      // `inReal`/`inImag` hold the packed x2 spectrum (same layout Forward
      // writes); outSamples receives N time-domain values.
      void Inverse(const float* inReal, const float* inImag, int log2N, float* outSamples)
      {
         const int n = 1 << log2N;

         // Undo the packing (inverse of Forward's unscramble) so the core
         // sees plain complex bins. No 0.5 here: the caller has already
         // halved Forward's x2, so inReal/inImag are standard-DFT-scaled
         // and any further scaling would diverge from vDSP.
         mStageRe[0] = inReal[0];
         mStageIm[0] = 0.0f;
         mStageRe[n / 2] = inImag[0];
         mStageIm[n / 2] = 0.0f;
         for (int k = 1; k < n / 2; k++)
         {
            mStageRe[k] = inReal[k];
            mStageIm[k] = inImag[k];
         }
         for (int k = n / 2 + 1; k < n; k++) // Hermitian mirror
         {
            mStageRe[k] = mStageRe[n - k];
            mStageIm[k] = -mStageIm[n - k];
         }

         RunCore(mStageRe.data(), mStageIm.data(), log2N, true, /*inverse=*/true);

         // Deliberately unnormalised, matching vDSP_fft_zrip(FFT_INVERSE):
         // the core accumulates the raw sum (= N*x). PaulStretchNode's
         // normScale = 1/(FFTSize*1.5) is shared by both backends and is what
         // takes this back down, together with the 75% Hann^2 overlap-add.
         // Dividing by N here would make Windows output 1/N of macOS.
         for (int i = 0; i < n; i++)
            outSamples[i] = mRe[i];
      }

   private:
      // Decimation-in-time, in place, on either a real-only input (imag =
      // 0) or full complex data. Reads/writes mRe/mIm[0..n), so inReal/inImag
      // must NOT alias them - see the staging buffers in Prepare().
      //
      // `inverse` conjugates the twiddle factors. The tables are built at a
      // negative angle (the forward kernel), so without this the "inverse"
      // is another forward transform, which over a Hermitian-mirrored
      // spectrum returns the input TIME-REVERSED rather than reconstructed.
      void RunCore(const float* inReal, const float* inImag, int log2N, bool complexIn,
                   bool inverse = false)
      {
         const int n = 1 << log2N;
         const int revShift = mMaxLog2 - log2N;
         const int twiddleStep = 1 << revShift;

         for (int i = 0; i < n; i++)
         {
            const int r = mRev[i << revShift];
            mRe[i] = inReal[r];
            mIm[i] = complexIn ? inImag[r] : 0.0f;
         }

         for (int size = 2; size <= n; size <<= 1)
         {
            const int half = size >> 1;
            const int step = twiddleStep * (n / size);
            for (int start = 0; start < n; start += size)
            {
               for (int j = 0; j < half; j++)
               {
                  const int tw = j * step;
                  const float wr = mCos[tw];
                  // Tables hold exp(-i*theta); negating the sine gives the
                  // conjugate exp(+i*theta) the inverse transform needs.
                  const float wi = inverse ? -mSin[tw] : mSin[tw];
                  const int a = start + j;
                  const int b = a + half;
                  const float tr = mRe[b] * wr - mIm[b] * wi;
                  const float ti = mRe[b] * wi + mIm[b] * wr;
                  mRe[b] = mRe[a] - tr;
                  mIm[b] = mIm[a] - ti;
                  mRe[a] += tr;
                  mIm[a] += ti;
               }
            }
         }
      }

      int mMaxLog2 = 0;
      std::vector<int> mRev;
      std::vector<float> mCos, mSin;
      std::vector<float> mRe, mIm;           // core scratch, sized to MaxSize
      std::vector<float> mStageRe, mStageIm; // Inverse()'s input staging

 public:
      RealFft() = default;
      RealFft(const RealFft&) = delete;
      RealFft& operator=(const RealFft&) = delete;
   };

   // Matches vDSP_hann_window with vDSP_HANN_NORM: denominator N (not N-1).
   inline void HannWindowNorm(float* out, int n)
   {
      for (int i = 0; i < n; i++)
         out[i] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265358979323846f * (float)i / (float)n));
   }
}
