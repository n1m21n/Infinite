#include "PortableFftFixture.h"

#include "PortableFft.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

bool RunPortableFftFixture()
{
   // PortableFft is the non-Apple backend for PaulStretchNode and MolderDsp,
   // and it must be a DROP-IN for the vDSP calls those files make on Apple:
   // the normalisation that follows the #if is shared, so a backend that
   // differs by a constant factor - or that returns the signal reversed -
   // still "works" everywhere except in the output samples.
   //
   // This fixture exists because the PaulStretch fixture could not see any of
   // that. It printed OK on a real Windows runner while Inverse() was
   // computing a forward transform over a mirrored spectrum, i.e. returning
   // the input time-reversed. Assert the algebra directly instead:
   //
   //   Forward:  x2-scaled split spectrum, zrip packing (DC in realp[0],
   //             Nyquist in imagp[0]).
   //   Inverse:  UNNORMALISED, like vDSP_fft_zrip(FFT_INVERSE) - so feeding
   //             back a standard-DFT-scaled spectrum yields N*x, and both
   //             the reversal bug and any stray 1/N or 0.5 show up here.
   bool ok = true;

   for (int log2n = 5; log2n <= 12; log2n++)
   {
      const int n = 1 << log2n;
      const int half = n / 2;

      // Deterministic pseudo-random input - no <random> so the fixture cannot
      // drift with a library implementation.
      std::vector<float> in(n);
      uint32_t seed = 0x9E3779B9u ^ (uint32_t)log2n;
      for (int i = 0; i < n; i++)
      {
         seed = seed * 1664525u + 1013904223u;
         in[i] = (float)((double)(seed >> 8) / 8388608.0 - 1.0); // [-1, 1)
      }

      PortableFft::RealFft fft;
      if (!fft.Prepare(log2n))
      {
         printf("[FAIL] PortableFft Prepare(%d) refused\n", log2n);
         ok = false;
         continue;
      }

      std::vector<float> re(half), im(half);
      fft.Forward(in.data(), log2n, re.data(), im.data());

      // A pure DC input pins Forward's scaling and packing: X_0 = sum(x), and
      // zrip reports 2*X_0 in realp[0] with every other bin ~0.
      std::vector<float> dc(n, 0.25f), dcRe(half), dcIm(half);
      fft.Forward(dc.data(), log2n, dcRe.data(), dcIm.data());
      const float expectDc = 2.0f * 0.25f * (float)n;
      if (std::fabs(dcRe[0] - expectDc) > expectDc * 1e-4f)
      {
         printf("[FAIL] PortableFft N=%d DC bin %.3f, expected %.3f (x2 packing lost)\n",
                n, dcRe[0], expectDc);
         ok = false;
      }
      for (int k = 1; k < half; k++)
      {
         if (std::fabs(dcRe[k]) > expectDc * 1e-4f || std::fabs(dcIm[k]) > expectDc * 1e-4f)
         {
            printf("[FAIL] PortableFft N=%d DC input leaked into bin %d\n", n, k);
            ok = false;
            break;
         }
      }

      // Round trip. Halving Forward's x2 is exactly what PaulStretchNode's
      // passthrough path does before reconstructing.
      std::vector<float> hRe(half), hIm(half), out(n);
      for (int k = 0; k < half; k++)
      {
         hRe[k] = re[k] * 0.5f;
         hIm[k] = im[k] * 0.5f;
      }
      fft.Inverse(hRe.data(), hIm.data(), log2n, out.data());

      double num = 0.0, den = 0.0, revNum = 0.0;
      for (int i = 0; i < n; i++)
      {
         const double want = (double)in[i] * (double)n; // unnormalised inverse
         const double d = (double)out[i] - want;
         num += d * d;
         den += want * want;
         const double dRev = (double)out[i] - (double)in[n - 1 - i] * (double)n;
         revNum += dRev * dRev;
      }
      const double err = std::sqrt(num / (den > 0.0 ? den : 1.0));
      const double revErr = std::sqrt(revNum / (den > 0.0 ? den : 1.0));

      if (err > 1e-4)
      {
         printf("[FAIL] PortableFft N=%d round trip rel err %.3e (want < 1e-4)\n", n, err);
         // Name the classic failure explicitly - it is the one that shipped.
         if (revErr < err)
            printf("       output matches reverse(input) (%.3e): Inverse is running the "
                   "forward kernel\n", revErr);
         ok = false;
      }
   }

   printf("%s\n", ok ? "[ok] PortableFft matches vDSP conventions in both directions"
                      : "[FAIL] PortableFft");
   return ok;
}

