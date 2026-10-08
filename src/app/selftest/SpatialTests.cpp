// INFINITE_SPATIALTEST: headless proof of the Spatial Mixer's binaural cues
// (docs/plans/spatial/README.md v2 benchmarks, Accuracy pillar). Renders the
// real node's audio half with synthetic signals that have known answers.
// Gated as an early exit before glfwInit(), like INFINITE_DSPTEST.
#include "app/AppShared.h"
#include "audio/dsp/SpatialMaster.h"

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
      // Pink-ish (-3 dB/oct) noise, the spectral tilt of music and speech: loudness
      // is judged on that, not on white noise whose energy sits above 6 kHz.
      std::vector<float> pink(N);
      {
         double b0 = 0, b1 = 0, b2 = 0;
         for (int i = 0; i < N; i++)
         {
            const double w = noise[i];
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            pink[i] = (float)((b0 + b1 + b2 + w * 0.1848) * 0.05); // well under the limiter
         }
      }
      // 5. Loudness flat +-1.5 dB round the circle, measured the way the node's
      // own meter measures it (K-weighted, both ears summed).
      {
         double lo = 1e9, hi = -1e9;
         for (int az = 0; az < 360; az += 30)
         {
            Rig r; place(r, (float)az, 0, 1); r.node.limiter = false;
            std::vector<float> L, R; r.Render(pink, L, R, [](int) {});
            SpatialMaster::Loudness m; m.Prepare(kSr);
            for (int i = 0; i < N; i++) m.Process(L[i], R[i]);
            const double p = m.ShortTerm();
            lo = std::min(lo, p); hi = std::max(hi, p);
         }
         check(hi - lo <= 1.5, "loudness spread round circle (K-weighted dB, target <=1.5)", hi - lo);
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
      // 10. BS.1770 calibration: a 997 Hz full-scale sine in one channel is -3.01 LUFS.
      {
         SpatialMaster::Loudness m; m.Prepare(kSr);
         for (int i = 0; i < (int)kSr * 4; i++)
            m.Process((float)std::sin(2.0 * M_PI * 997.0 * i / kSr), 0.0f);
         check(std::fabs(m.ShortTerm() + 3.01) < 0.1, "LUFS of 0 dBFS 997 Hz sine, one channel (want -3.01)", m.ShortTerm());
      }
      // 11. Limiter: a +14 dB hot source stays under the -1 dBTP ceiling; with the limiter off it does not.
      {
         double peaks[2];
         for (int on = 0; on < 2; on++)
         {
            Rig r; place(r, 0, 0, 1);
            r.node.gainDb[0] = 14.0f; r.node.limiter = (on == 1);
            std::vector<float> L, R; r.Render(noise, L, R, [](int) {});
            double pk = 0;
            for (int i = skip; i < N; i++) pk = std::max(pk, (double)std::max(std::fabs(L[i]), std::fabs(R[i])));
            peaks[on] = pk;
         }
         check(peaks[1] <= 0.8913 + 1e-3 && peaks[0] > 1.0, "limiter holds -1 dBFS ceiling (on, off peaks)", peaks[1]);
      }
      // 12. Room: an impulse leaves a tail with room, none without.
      {
         double tail[2];
         for (int on = 0; on < 2; on++)
         {
            Rig r; place(r, 30, 0, 2); r.node.room = on ? 0.8f : 0.0f;
            std::vector<float> imp(N, 0.0f); imp[1000] = 1.0f;
            std::vector<float> L, R; r.Render(imp, L, R, [](int) {});
            double e = 0; for (int i = 6000; i < N - 256; i++) e += (double)L[i] * L[i] + (double)R[i] * R[i];
            tail[on] = e;
         }
         check(tail[0] < 1e-9 && tail[1] > 1e-6, "room adds a tail (tail energy off, on)", tail[1]);
      }
      // 13. Bass mono: a 40 Hz tone at 90 deg is level in both ears with bass mono, not without.
      {
         double ild[2];
         std::vector<float> sine(N);
         for (int i = 0; i < N; i++) sine[i] = 0.5f * (float)std::sin(2.0 * M_PI * 40.0 * i / kSr);
         for (int on = 0; on < 2; on++)
         {
            Rig r; place(r, 90, 0, 1); r.node.bassHz = on ? 300.0f : 0.0f; r.node.limiter = false;
            std::vector<float> L, R; r.Render(sine, L, R, [](int) {});
            ild[on] = std::fabs(Db(Rms(R, skip)) - Db(Rms(L, skip)));
         }
         check(ild[1] < 0.5 && ild[0] > ild[1], "bass mono evens the ears at 40 Hz (|dB R-L| off, on)", ild[1]);
      }
      // 14. Head tracking: yaw +90 turns a front source to the left ear; head-locked stays put;
      // the export file stays facing front.
      {
         double d[3];
         for (int mode = 0; mode < 3; mode++)
         {
            Rig r; place(r, 0, 0, 1); r.node.trackMode = 1; r.node.SetHeadYaw(90.0f);
            r.node.headLocked[0] = (mode == 1);
            if (mode == 2) r.node.CaptureRing().enabled.store(true);
            std::vector<float> L, R; r.Render(noise, L, R, [](int) {});
            d[mode] = Db(Rms(R, skip)) - Db(Rms(L, skip));
            if (mode == 2)
            {
               std::vector<float> ring(N * 2 + 16);
               int got = r.node.CaptureRing().Read(ring.data(), (int)ring.size());
               double fl = 0, fr = 0; for (int i = skip; i < got / 2; i++) { fl += ring[2 * i] * ring[2 * i]; fr += ring[2 * i + 1] * ring[2 * i + 1]; }
               check(got > N && std::fabs(10 * std::log10(fr / fl)) < 0.5, "export file faces front while monitor is turned (|dB R-L|)", std::fabs(10 * std::log10(fr / fl)));
            }
         }
         check(d[0] < -6.0, "yaw +90 puts a front source in the left ear (dB R-L)", d[0]);
         check(std::fabs(d[1]) < 0.5, "head-locked source ignores yaw (|dB R-L|)", std::fabs(d[1]));
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
      // 24-bit export: header says 24, data is 3 bytes/sample, and a known
      // sample reads back within one 24-bit step.
      {
         const std::string path = "/tmp/infinite_spatial_24.wav";
         AudioFileWriter w;
         std::vector<float> x(2000);
         for (size_t i = 0; i < x.size(); i++)
            x[i] = 0.5f * std::sin(0.05f * (float)i);
         bool good = w.Open(path, 48000.0, 2, AudioFileWriter::Format::Wav, 24);
         if (good)
         {
            w.Append(x.data(), 1000);
            w.Close();
         }
         FILE* f = good ? fopen(path.c_str(), "rb") : nullptr;
         double err = 1.0;
         if (f)
         {
            unsigned char h[44];
            std::vector<unsigned char> d(6000);
            if (fread(h, 1, 44, f) == 44 && fread(d.data(), 1, 6000, f) == 6000)
            {
               const int bits = h[34] | (h[35] << 8);
               const unsigned dataBytes = h[40] | (h[41] << 8) | (h[42] << 16) | ((unsigned)h[43] << 24);
               const size_t k = 777;
               int v = d[k * 3] | (d[k * 3 + 1] << 8) | (d[k * 3 + 2] << 16);
               if (v & 0x800000) v -= 0x1000000;
               err = bits == 24 && dataBytes == 6000 ? std::fabs((double)v / 8388607.0 - (double)x[k]) : 1.0;
            }
            fclose(f);
            remove(path.c_str());
         }
         check(err < 2e-7, "24-bit WAV export reads back (abs error)", err);
      }
      printf("SPATIALTEST %s\n", ok ? "OK" : "FAIL");
      return ok;
   }
}
