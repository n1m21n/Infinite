// INFINITE_SPATIALTEST: headless proof of the Spatial Mixer's binaural cues
// (docs/plans/spatial/README.md v2 benchmarks, Accuracy pillar). Renders the
// real node's audio half with synthetic signals that have known answers.
// Gated as an early exit before glfwInit(), like INFINITE_DSPTEST.
#include "app/AppShared.h"

namespace app
{
   namespace
   {
      constexpr int kBlock = 256;
      constexpr double kSr = 48000.0;

      struct Rig
      {
         SpatialMixerNode node;
         AudioNode* audio = nullptr;
         int frame = 0;

         Rig()
         {
            audio = node.GetAudioNode();
            audio->PrepareToPlay(kSr, kBlock);
         }

         // Renders `total` frames of mono `src` as input 0; fills L/R.
         // `perBlock` runs before each block (e.g. to move the object).
         template <class F>
         void Render(const std::vector<float>& src, std::vector<float>& L, std::vector<float>& R, F perBlock)
         {
            const int total = (int)src.size();
            L.assign(total, 0.0f);
            R.assign(total, 0.0f);
            std::vector<float> in(kBlock), oL(kBlock), oR(kBlock);
            for (int pos = 0; pos + kBlock <= total; pos += kBlock)
            {
               perBlock(pos);
               node.CookIfNeeded(++frame);
               std::copy(src.begin() + pos, src.begin() + pos + kBlock, in.begin());
               float* inCh[1] = { in.data() };
               AudioBuffer inBuf { inCh, 1, kBlock };
               const AudioBuffer* inputs[1] = { &inBuf };
               float* outCh[2] = { oL.data(), oR.data() };
               AudioBuffer out { outCh, 2, kBlock };
               audio->ProcessBlock(inputs, 1, out);
               std::copy(oL.begin(), oL.end(), L.begin() + pos);
               std::copy(oR.begin(), oR.end(), R.begin() + pos);
            }
         }
      };

      std::vector<float> Noise(int n, unsigned seed = 1)
      {
         std::vector<float> v(n);
         unsigned s = seed;
         for (float& x : v)
         {
            s = s * 1664525u + 1013904223u;
            x = ((float)(s >> 8) / (float)(1 << 24)) * 2.0f - 1.0f;
         }
         return v;
      }

      double Rms(const std::vector<float>& v, int from)
      {
         double a = 0;
         for (size_t i = from; i < v.size(); i++) a += (double)v[i] * v[i];
         return std::sqrt(a / std::max<size_t>(1, v.size() - from));
      }
      // High-passed (first difference) energy: a crude >~8 kHz proxy.
      double HfRms(const std::vector<float>& v, int from)
      {
         double a = 0;
         for (size_t i = from + 1; i < v.size(); i++) { const double d = v[i] - v[i - 1]; a += d * d; }
         return std::sqrt(a / std::max<size_t>(1, v.size() - from));
      }
      double Db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }
   }

   namespace
   {
      // Unit-amplitude noise source for the engine-level check.
      class NoiseSrc : public AudioNode
      {
      public:
         void ProcessBlock(const AudioBuffer* const*, int, AudioBuffer& b) override
         {
            for (int i = 0; i < b.numFrames; i++)
            {
               mS = mS * 1664525u + 1013904223u;
               const float x = ((float)(mS >> 8) / (float)(1 << 24)) * 2.0f - 1.0f;
               for (int c = 0; c < b.numChannels; c++) b.channels[c][i] = x;
            }
         }
      private:
         unsigned mS = 7;
      };
   }

   bool RunSpatialFixture()
   {
      setvbuf(stdout, nullptr, _IONBF, 0);
      bool ok = true;
      auto check = [&](bool c, const char* what, double v) {
         printf("SPATIALTEST %s: %.3f  %s\n", what, v, c ? "OK" : "FAIL");
         ok = ok && c;
      };
      const int N = 48000;
      const int skip = 4800;
      const std::vector<float> noise = Noise(N);

      auto place = [](Rig& r, float az, float el, float dist) {
         r.node.azimuth[0] = az; r.node.elevation[0] = el; r.node.distance[0] = dist;
         r.node.width[0] = 0.0f; r.node.gainDb[0] = 0.0f;
      };

      // 1. Front is symmetric.
      {
         Rig r; place(r, 0, 0, 1);
         std::vector<float> L, R; r.Render(noise, L, R, [](int) {});
         double d = 0; for (int i = skip; i < N; i++) d += std::fabs(L[i] - R[i]);
         check(d / N < 1e-4, "front L==R (mean |L-R|)", d / N);
      }
      // 2+3. Side: ITD ~0.65 ms (31 samples) and ILD >= 6 dB, right ear louder.
      {
         Rig r; place(r, 90, 0, 1);
         std::vector<float> L, R; r.Render(noise, L, R, [](int) {});
         int bestLag = 0; double best = -1e30;
         for (int lag = 0; lag <= 60; lag++)
         {
            double c = 0;
            for (int i = skip; i < N - 64; i++) c += (double)R[i] * L[i + lag];
            if (c > best) { best = c; bestLag = lag; }
         }
         check(bestLag >= 27 && bestLag <= 35, "ITD at 90 deg (samples, want ~31)", bestLag);
         const double ild = Db(Rms(R, skip)) - Db(Rms(L, skip));
         check(ild >= 6.0 && ild <= 20.0, "ILD at 90 deg (dB R-L)", ild);
      }
      // 4. Rear is darker than front (pinna cue).
      {
         Rig f, b; place(f, 0, 0, 1); place(b, 180, 0, 1);
         std::vector<float> fl, fr, bl, br;
         f.Render(noise, fl, fr, [](int) {}); b.Render(noise, bl, br, [](int) {});
         const double d = Db(HfRms(fl, skip)) - Db(HfRms(bl, skip));
         check(d >= 2.0, "front-back HF contrast (dB)", d);
      }
      // 5. Loudness flat +-1.5 dB round the circle (benchmark; power of both ears).
      {
         double lo = 1e9, hi = -1e9;
         for (int az = 0; az < 360; az += 45)
         {
            Rig r; place(r, (float)az, 0, 1);
            std::vector<float> L, R; r.Render(noise, L, R, [](int) {});
            const double p = Db(std::sqrt(Rms(L, skip) * Rms(L, skip) + Rms(R, skip) * Rms(R, skip)));
            printf("  az %3d: %.2f dB\n", az, p);
            lo = std::min(lo, p); hi = std::max(hi, p);
         }
         check(hi - lo <= 3.0, "loudness spread round circle (dB, target <=3 now, <=1.5 with HRTF EQ)", hi - lo);
      }
      // 6. Distance: doubling 2 m -> 4 m drops ~6 dB.
      {
         Rig a, b; place(a, 0, 0, 2); place(b, 0, 0, 4);
         std::vector<float> al, ar, bl, br;
         a.Render(noise, al, ar, [](int) {}); b.Render(noise, bl, br, [](int) {});
         const double d = Db(Rms(al, skip)) - Db(Rms(bl, skip));
         check(d >= 5.0 && d <= 8.0, "distance 2->4 m (dB drop)", d);
      }
      // 7. 360 deg / 1 s sweep of a sine: no clicks (sample-to-sample jump bounded).
      {
         Rig r; place(r, 0, 0, 1);
         std::vector<float> sine(N);
         for (int i = 0; i < N; i++) sine[i] = std::sin(2.0 * M_PI * 200.0 * i / kSr);
         std::vector<float> L, R;
         r.Render(sine, L, R, [&](int pos) { r.node.azimuth[0] = (float)pos / (float)N * 360.0f - 180.0f; });
         double worst = 0; int at = 0;
         for (int i = skip; i < 47872; i++)
         {
            const double st = std::max(std::fabs(L[i] - L[i - 1]), std::fabs(R[i] - R[i - 1]));
            if (st > worst) { worst = st; at = i; }
         }
         printf("  worst step at frame %d (az %.0f)\n", at, (double)at / N * 360.0 - 180.0);
         // A clean 200 Hz unit sine moves <= 0.0262/sample; head boost allows ~1.25x.
         check(worst < 0.06, "sweep max sample step (click if large)", worst);
      }
      // 8. No connected input renders silence.
      {
         Rig r; std::vector<float> L(kBlock), R(kBlock);
         float* outCh[2] = { L.data(), R.data() };
         AudioBuffer out { outCh, 2, kBlock };
         const AudioBuffer* inputs[1] = { nullptr };
         r.audio->ProcessBlock(inputs, 1, out);
         check(Rms(L, 0) == 0.0 && Rms(R, 0) == 0.0, "unconnected input silent", Rms(L, 0));
      }
      // 9. Through the real engine as a terminal output (no output pin): the
      // node's own buffer reaches the device mix and is binaural (R louder
      // than L for a source at +90).
      {
         NoiseSrc src;
         SpatialMixerNode sm;
         sm.azimuth[0] = 90.0f; sm.elevation[0] = 0.0f; sm.distance[0] = 1.0f; sm.width[0] = 0.0f;
         AudioNode* smAudio = sm.GetAudioNode();
         src.PrepareToPlay(kSr, kBlock);
         smAudio->PrepareToPlay(kSr, kBlock);
         AudioTopology topo;
         AudioTopologyEntry se; se.node = &src; se.outputBufferIndex = 0;
         topo.order.push_back(se);
         AudioTopologyEntry me; me.node = smAudio; me.numInputs = 1; me.inputBufferIndices[0] = 0; me.outputBufferIndex = 1;
         topo.order.push_back(me);
         topo.numBuffers = 2;
         topo.terminalBufferIndices.push_back({ 1, nullptr });
         AudioEngine::Instance().SetTopology(topo);
         std::vector<float> c0(kBlock), c1(kBlock);
         float* ch[2] = { c0.data(), c1.data() };
         AudioBuffer buf { ch, 2, kBlock };
         std::vector<float> L, R;
         for (int b = 0; b < 40; b++)
         {
            sm.CookIfNeeded(b + 1);
            AudioEngine::Instance().ProcessOffline(buf);
            L.insert(L.end(), c0.begin(), c0.end());
            R.insert(R.end(), c1.begin(), c1.end());
         }
         const double ild = Db(Rms(R, 2048)) - Db(Rms(L, 2048));
         check(ild >= 6.0, "engine terminal path renders binaural (dB R-L)", ild);
         AudioTopology empty;
         AudioEngine::Instance().SetTopology(empty);
      }
      printf("SPATIALTEST %s\n", ok ? "OK" : "FAIL");
      return ok;
   }
}
