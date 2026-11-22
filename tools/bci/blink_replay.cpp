// M4 replay: feed a 2-channel frontal recording through the real BciDspEngine and print the
// time of every rising edge of the Blink pin (pin 18). Fp1 -> AF7, Fp2 -> AF8, TP9/TP10 = 0.
// Build + score: tools/bci/blink_m4.py (compiles this with the engine sources, no app build).
//
// usage: blink_replay FS MODE SIGN [CAL_SEC] < samples.txt     (one "fp1 fp2" line per sample, uV)
//   MODE 0 = IntentionalOrDouble, 1 = AllBlinks, 2 = DoubleBlink; SIGN multiplies the input (+1 / -1).
//   CAL_SEC > 2: seconds 2..CAL_SEC are the prompted-blink calibration phase.
#include "core/bci/BciDspEngine.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
   if (argc < 4)
      return 2;
   const double fs = std::atof(argv[1]);
   const int mode = std::atoi(argv[2]);
   const float sign = (float)std::atof(argv[3]);
   const double calSec = argc > 4 ? std::atof(argv[4]) : 0.0;

   std::vector<Bci::SampleFrame> frames;
   double a, b;
   while (std::scanf("%lf %lf", &a, &b) == 2)
   {
      Bci::SampleFrame f;
      f.ch[0] = sign * (float)a;
      f.ch[1] = sign * (float)b;
      frames.push_back(f);
   }

   Bci::BciDspEngine eng;
   eng.Reset(fs);
   eng.SetPersistProfile(false);
   eng.SetProfile(Bci::CalibrationProfile{}); // defaults, not the user's saved profile
   eng.SetStartleGuard(false);
   const Bci::BlinkMode bm = mode == 2 ? Bci::BlinkMode::DoubleBlink
                           : mode == 1 ? Bci::BlinkMode::AllBlinks
                                       : Bci::BlinkMode::IntentionalOrDouble;
   eng.SetBlinkMode(bm);

   float pins[Bci::kOutputPinCount] = {};
   Bci::TelemetrySnapshot tel;
   const int batch = 12; // engine batches ~12 samples per update on hardware
   bool prev = false;
   bool calStarted = false;
   for (size_t i = 0; i < frames.size(); i += batch)
   {
      // Filters settle for 2 s first (in the app the phase follows 60 s of eyes open / closed).
      if (calSec > 2.0 && !calStarted && (double)i / fs >= 2.0)
      {
         eng.StartBlinkCalibration(calSec - (double)i / fs);
         calStarted = true;
      }
      const int n = (int)std::min<size_t>(batch, frames.size() - i);
      eng.Process(frames.data() + i, n, pins, tel);
      const bool on = pins[18] > 0.5f;
      if (on && !prev)
         std::printf("%.4f\n", (double)(i + n) / fs);
      prev = on;
   }
   const auto& pr = eng.Profile();
   std::fprintf(stderr, "blinkValid %d sign %+.0f median %.1f uV width %.0f ms\n", pr.blinkValid ? 1 : 0, pr.blinkSign,
                pr.medianBlinkUv, pr.blinkWidthMs);
   return 0;
}
