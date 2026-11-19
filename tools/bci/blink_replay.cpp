// M4 replay: feed a 2-channel frontal recording through the real BciDspEngine and print the
// time of every rising edge of the Blink pin (pin 18). Fp1 -> AF7, Fp2 -> AF8, TP9/TP10 = 0.
// Build + score: tools/bci/blink_m4.py (compiles this with the engine sources, no app build).
//
// usage: blink_replay FS MODE SIGN < samples.txt     (one "fp1 fp2" line per sample, uV)
//   MODE 0 = IntentionalOrDouble, 1 = AllBlinks; SIGN multiplies the input (+1 / -1).
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
   eng.SetBlinkMode(mode ? Bci::BlinkMode::AllBlinks : Bci::BlinkMode::IntentionalOrDouble);

   float pins[Bci::kOutputPinCount] = {};
   Bci::TelemetrySnapshot tel;
   const int batch = 12; // engine batches ~12 samples per update on hardware
   bool prev = false;
   for (size_t i = 0; i < frames.size(); i += batch)
   {
      const int n = (int)std::min<size_t>(batch, frames.size() - i);
      eng.Process(frames.data() + i, n, pins, tel);
      const bool on = pins[18] > 0.5f;
      if (on && !prev)
         std::printf("%.4f\n", (double)(i + n) / fs);
      prev = on;
   }
   std::fprintf(stderr, "medianBlinkUv %.1f\n", eng.Profile().medianBlinkUv);
   return 0;
}
