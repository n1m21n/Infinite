// DSP self-test fixtures, part 2: sampler..granular (moved verbatim from main.cpp).
#include "app/AppShared.h"
#include "audio/dsp/ConvolutionKernel.h"
#include "audio/dsp/SimdF4.h"
#include "audio/MusicTime.h"

#include <chrono>
#include <filesystem>

namespace app
{
// Sampler's DSP fixture (new-audio-node/SKILL.md §5): loads a real WAV
// through the actual Platform::DecodeAudioFileToBuffer path (not a synthetic
// in-memory buffer, so the decoder itself is exercised too), triggers
// playback via a real NoteEventQueue the way a note cable would, and asserts
// each param produces a measurable difference in the rendered samples - the
// same signature-diffing approach AUDIOPARAMSWEEPTEST uses, but against a
// Sampler that actually has a file loaded, which the generic sweep's bare
// throwaway instance never does (see driver.sh's Sampler baseline note for
// why the sweep alone reports [FAIL] on this node).
bool RunSamplerFixture()
{
   const int numFrames = 4410;    // 0.1s @ 44100 Hz
   const int sampleRate = 44100;
   std::vector<int16_t> pcm(numFrames);
   for (int i = 0; i < numFrames; i++)
   {
      const float t = (float)i / (float)(numFrames - 1); // 0..1 ramp
      pcm[i] = (int16_t)((t * 2.0f - 1.0f) * 32000.0f);
   }

   const std::string path = TmpPath("infinite_sampler_fixture.wav");
   {
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
   }

   bool ok = true;

   struct Params
   {
      float pitch = 0.0f, finetune = 0.0f, speed = 1.0f, volume = 0.8f, start = 0.0f, end = 1.0f;
      bool loop = false, reverse = false, pingpong = false;
   };

   // Triggers one note-on through a real NoteEventQueue (the same path a
   // connected note cable drives) and renders `frames` samples.
   auto trigger = [&](const Params& p, int frames) -> std::vector<float>
   {
      SamplerNode node;
      if (!node.LoadFile(path))
      {
         ok = false;
         return {};
      }
      node.pitch = p.pitch;
      node.finetune = p.finetune;
      node.speed = p.speed;
      node.volume = p.volume;
      node.start = p.start;
      node.end = p.end;
      node.loop = p.loop;
      node.reverse = p.reverse;
      node.pingpong = p.pingpong;
      node.CookIfNeeded(1);

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, frames);
      node.CookIfNeeded(2);

      NoteEventQueue queue;
      const int cursor = queue.RegisterConsumer();
      an->SetNoteInbox(&queue, cursor);
      NoteEvent on;
      on.note = 60;
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.frameOffset = 0;
      queue.Push(on);

      std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      an->ProcessBlock(nullptr, 0, buf);
      return l;
   };

   auto differs = [](const std::vector<float>& a, const std::vector<float>& b)
   {
      if (a.size() != b.size())
         return true;
      for (size_t i = 0; i < a.size(); i++)
         if (std::fabs(a[i] - b[i]) > 1e-4f)
            return true;
      return false;
   };

   Params base;
   const auto baseline = trigger(base, 64);
   Params withPitch = base; withPitch.pitch = 12.0f;        // +1 octave: rate doubles
   Params withFinetune = base; withFinetune.finetune = 50.0f; // +50c: same direction as pitch, smaller
   Params withSpeed = base; withSpeed.speed = 2.0f;          // varispeed: rate doubles like +1 octave
   Params withStart = base; withStart.start = 0.5f;          // trigger halfway through the ramp
   Params withVolume = base; withVolume.volume = 0.2f;       // quarter volume

   const auto pitched = trigger(withPitch, 64);
   const auto finetuned = trigger(withFinetune, 64);
   const auto sped = trigger(withSpeed, 64);
   const auto started = trigger(withStart, 64);
   const auto quiet = trigger(withVolume, 64);

   if (baseline.empty() || pitched.empty() || finetuned.empty() || sped.empty() || started.empty() || quiet.empty())
   {
      printf("SAMPLERTEST BUG (fixture failed to load its own test file)\n");
      return false;
   }
   if (!differs(baseline, pitched)) { printf("SAMPLERTEST pitch param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, finetuned)) { printf("SAMPLERTEST finetune param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, sped)) { printf("SAMPLERTEST speed param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, started)) { printf("SAMPLERTEST start param had no effect FAIL\n"); ok = false; }
   if (!differs(baseline, quiet)) { printf("SAMPLERTEST volume param had no effect FAIL\n"); ok = false; }

   // Reverse: triggering with reverse=true starts at `end` and plays toward
   // `start`, so on this monotonic ramp its first samples should sit near
   // the ramp's *top* rather than its bottom - the opposite of forward.
   {
      Params fwd = base;
      Params rev = base; rev.reverse = true;
      // 64 frames, not 4: the per-pass fade-in scales sample 0 to exactly 0 in
      // both directions, so the first samples cannot tell them apart.
      const auto f = trigger(fwd, 64);
      const auto r = trigger(rev, 64);
      if (f.empty() || r.empty() || !(r[63] > f[63]))
      {
         printf("SAMPLERTEST reverse param had no effect FAIL\n");
         ok = false;
      }
   }

   // Loop vs. end vs. ping-pong: render well past the sample's natural end
   // and check each shapes the tail differently - loop wraps back to
   // silence-then-ramp-up again, ping-pong instead keeps descending from
   // wherever it bounced, so the two must diverge past the wrap point.
   {
      auto renderWholeFile = [&](const Params& p) -> std::vector<float>
      {
         SamplerNode node;
         node.LoadFile(path);
         node.pitch = p.pitch; node.finetune = p.finetune; node.speed = p.speed; node.volume = p.volume;
         node.start = p.start; node.end = p.end; node.loop = p.loop; node.reverse = p.reverse;
         node.pingpong = p.pingpong;
         node.CookIfNeeded(1);
         AudioNode* an = node.GetAudioNode();
         const int frames = numFrames + 200;
         an->PrepareToPlay((double)sampleRate, frames);
         node.CookIfNeeded(2);
         NoteEventQueue queue;
         const int cursor = queue.RegisterConsumer();
         an->SetNoteInbox(&queue, cursor);
         NoteEvent on;
         on.note = 60;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         queue.Push(on);
         std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = frames;
         an->ProcessBlock(nullptr, 0, buf);
         return l;
      };

      Params looped = base; looped.loop = true;
      Params oneShot = base;
      Params pingponged = base; pingponged.loop = true; pingponged.pingpong = true;

      const auto loopedTail = renderWholeFile(looped);
      const auto oneShotTail = renderWholeFile(oneShot);
      const auto pingpongTail = renderWholeFile(pingponged);
      if (!(std::fabs(loopedTail[numFrames + 100]) > 0.05f && std::fabs(oneShotTail[numFrames + 100]) < 0.05f))
      {
         printf("SAMPLERTEST loop param had no effect (looped=%.3f oneshot=%.3f) FAIL\n",
                loopedTail[numFrames + 100], oneShotTail[numFrames + 100]);
         ok = false;
      }
      if (!differs(loopedTail, pingpongTail))
      {
         printf("SAMPLERTEST pingpong param had no effect FAIL\n");
         ok = false;
      }
   }

   // end: shrinking the loop range must change where playback wraps, so a
   // looped voice with end=0.5 must sound different past the halfway point
   // than the full-range loop above once both have wrapped at least once.
   {
      Params fullLoop = base; fullLoop.loop = true;
      Params shortLoop = base; shortLoop.loop = true; shortLoop.end = 0.5f;
      const auto full = trigger(fullLoop, numFrames / 2 + 50);
      const auto shortR = trigger(shortLoop, numFrames / 2 + 50);
      if (!differs(full, shortR)) { printf("SAMPLERTEST end param had no effect FAIL\n"); ok = false; }
   }

   // fade in / fade out: a long fade-in must pull the first samples of a pass
   // well below the un-faded ones, and a long fade-out must do the same to the
   // last samples before the range edge (one-shot, so the tail is the edge).
   {
      auto render = [&](float fi, float fo, int frames) -> std::vector<float>
      {
         SamplerNode node;
         node.LoadFile(path);
         node.fadeIn = fi;
         node.fadeOut = fo;
         node.CookIfNeeded(1);
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay((double)sampleRate, frames);
         node.CookIfNeeded(2);
         NoteEventQueue queue;
         const int cursor = queue.RegisterConsumer();
         an->SetNoteInbox(&queue, cursor);
         NoteEvent on;
         on.note = 60;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.frameOffset = 0;
         queue.Push(on);
         std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
         float* chans[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = frames;
         an->ProcessBlock(nullptr, 0, buf);
         return l;
      };
      // Ramp is -1..1: the first 400 frames of an un-faded pass sit near -1*0.8;
      // a 50 ms (2205-frame) fade-in scales that by <= 400/2205.
      const auto hard = render(0.0f, 0.0f, 600);
      const auto fadedIn = render(50.0f, 0.0f, 600);
      float hardMag = 0.0f, fadeMag = 0.0f;
      for (int i = 300; i < 600; i++)
      {
         hardMag = std::max(hardMag, std::fabs(hard[i]));
         fadeMag = std::max(fadeMag, std::fabs(fadedIn[i]));
      }
      if (!(fadeMag < hardMag * 0.5f))
      {
         printf("SAMPLERTEST fade in had no effect (hard=%.3f faded=%.3f) FAIL\n", hardMag, fadeMag);
         ok = false;
      }
      // Fade-out: end the sound at the range edge; the last 300 frames before
      // it must be quieter with a 50 ms fade-out than without.
      const auto hardTail = render(0.0f, 0.0f, numFrames - 20);
      const auto fadedTail = render(0.0f, 50.0f, numFrames - 20);
      float hardTailMag = 0.0f, fadeTailMag = 0.0f;
      for (int i = numFrames - 320; i < numFrames - 20; i++)
      {
         hardTailMag = std::max(hardTailMag, std::fabs(hardTail[i]));
         fadeTailMag = std::max(fadeTailMag, std::fabs(fadedTail[i]));
      }
      if (!(fadeTailMag < hardTailMag * 0.5f))
      {
         printf("SAMPLERTEST fade out had no effect (hard=%.3f faded=%.3f) FAIL\n", hardTailMag, fadeTailMag);
         ok = false;
      }
   }

   // Record: arm recording, feed one block of a known synthetic tone
   // through the audio input pin (slot 1 - see AudioSamplerNode::
   // ProcessBlock's comment on why it's 1, not 0), stop, and confirm the
   // captured audio became the loaded buffer.
   {
      SamplerNode node;
      node.StartRecording();
      if (!node.IsRecording())
      {
         printf("SAMPLERTEST record param had no effect (StartRecording didn't arm) FAIL\n");
         ok = false;
      }
      else
      {
         AudioNode* an = node.GetAudioNode();
         an->PrepareToPlay((double)sampleRate, 128);
         std::vector<float> toneL(128), toneR(128);
         for (int i = 0; i < 128; i++)
            toneL[i] = toneR[i] = sinf((float)i * 0.3f);
         float* srcChans[2] = { toneL.data(), toneR.data() };
         AudioBuffer srcBuf;
         srcBuf.channels = srcChans;
         srcBuf.numChannels = 2;
         srcBuf.numFrames = 128;

         std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
         float* outChans[2] = { outL.data(), outR.data() };
         AudioBuffer outBuf;
         outBuf.channels = outChans;
         outBuf.numChannels = 2;
         outBuf.numFrames = 128;

         const AudioBuffer* inputs[2] = { nullptr, &srcBuf };
         an->ProcessBlock(inputs, 2, outBuf);

         node.StopRecording();
         if (node.FileName() != "recorded audio" || node.waveformCacheCount <= 0 || node.FilePath().empty())
         {
            printf("SAMPLERTEST record param had no effect (nothing captured) FAIL\n");
            ok = false;
         }
         else
         {
            SamplerNode reloaded;
            std::vector<std::pair<std::string, std::string>> params;
            Patch::SaveParams(&node, params);
            Patch::LoadParams(&reloaded, params);
            reloaded.ReloadFromPath();
            if (reloaded.waveformCacheCount <= 0 || reloaded.FilePath().empty())
            {
               printf("SAMPLERTEST record patch reload failed FAIL\n");
               ok = false;
            }
         }
      }
   }

   remove(path.c_str());

   printf("%s\n", ok ? "SAMPLERTEST OK" : "SAMPLERTEST FAIL");
   return ok;
}

// PaulStretch's DSP fixture: loads a real WAV through the actual
// Platform::DecodeAudioFileToBuffer path, triggers playback via a real
// NoteEventQueue, and asserts that stretch, pitch shift, fine tune, frequency
// shift, unison detune, and volume produce finite, valid audio outputs.
bool RunPaulStretchFixture()
{
   const int numFrames = 44100; // 1.0s @ 44100 Hz
   const int sampleRate = 44100;
   std::vector<int16_t> pcm(numFrames);
   for (int i = 0; i < numFrames; i++)
   {
      const float t = (float)i / (float)(numFrames - 1);
      pcm[i] = (int16_t)(std::sin(t * 440.0f * 6.283185f) * 30000.0f);
   }

   const std::string path = TmpPath("infinite_paulstretch_fixture.wav");
   {
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
   }

   bool ok = true;

   auto trigger = [&](float stretch, int winIdx, float pitch, float fine, float freq, int uni, float detune, float vol, int frames) -> std::vector<float>
   {
      PaulStretchNode node;
      if (!node.LoadFile(path))
      {
         ok = false;
         return {};
      }
      node.stretch = stretch;
      node.windowSizeIndex = winIdx;
      node.pitchShift = pitch;
      node.fineTune = fine;
      node.freqShift = freq;
      node.unison = uni;
      node.detune = detune;
      node.volume = vol;
      node.CookIfNeeded(1);

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, 2048);

      node.TriggerPreview(0.0f);
      node.CookIfNeeded(2);

      std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      for (int block = 0; block < 4; ++block)
         an->ProcessBlock(nullptr, 0, buf);
      return l;
   };

   // Render baseline with 2048-window size (index 0)
   const auto baseline = trigger(8.0f, 0, 0.0f, 0.0f, 0.0f, 1, 10.0f, 0.8f, 1024);
   const auto pitched = trigger(8.0f, 0, 7.0f, 0.0f, 0.0f, 1, 10.0f, 0.8f, 1024);
   const auto freqShifted = trigger(8.0f, 0, 0.0f, 0.0f, 200.0f, 1, 10.0f, 0.8f, 1024);
   const auto unisonSound = trigger(8.0f, 0, 0.0f, 0.0f, 0.0f, 4, 30.0f, 0.8f, 1024);

   if (baseline.empty() || pitched.empty() || freqShifted.empty() || unisonSound.empty())
   {
      printf("PAULSTRETCHTEST BUG (fixture failed to load test file)\n");
      ok = false;
   }
   else
   {
      for (float s : baseline)
      {
         if (!std::isfinite(s)) { printf("PAULSTRETCHTEST baseline produced non-finite sample FAIL\n"); ok = false; break; }
      }
      for (float s : pitched)
      {
         if (!std::isfinite(s)) { printf("PAULSTRETCHTEST pitched produced non-finite sample FAIL\n"); ok = false; break; }
      }

      // Record test: verify StartRecording arms and captures audio, writes WAV to disk, and reloads via Patch / ReloadFromPath()
      {
         PaulStretchNode recNode;
         recNode.StartRecording();
         if (!recNode.IsRecording())
         {
            printf("PAULSTRETCHTEST record arm failed FAIL\n");
            ok = false;
         }
         else
         {
            AudioNode* an = recNode.GetAudioNode();
            an->PrepareToPlay((double)sampleRate, 128);
            std::vector<float> tone(128);
            for (int i = 0; i < 128; i++)
               tone[i] = sinf((float)i * 0.3f);
            float* srcChans[1] = { tone.data() };
            AudioBuffer srcBuf;
            srcBuf.channels = srcChans;
            srcBuf.numChannels = 1;
            srcBuf.numFrames = 128;

            std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
            float* outChans[2] = { outL.data(), outR.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            const AudioBuffer* inputs[1] = { &srcBuf };
            an->ProcessBlock(inputs, 1, outBuf);

            recNode.StopRecording();
            if (recNode.waveformCacheCount <= 0 || recNode.FileName() != "recording" || recNode.FilePath().empty())
            {
               printf("PAULSTRETCHTEST recording buffer capture or file path failed FAIL\n");
               ok = false;
            }
            else
            {
               PaulStretchNode reloaded;
               recNode.position = 0.42f;
               std::vector<std::pair<std::string, std::string>> params;
               Patch::SaveParams(&recNode, params);
               Patch::LoadParams(&reloaded, params);
               reloaded.ReloadFromPath();
               if (reloaded.waveformCacheCount <= 0 || reloaded.FilePath().empty() || std::abs(reloaded.position - 0.42f) > 1e-4f)
               {
                  printf("PAULSTRETCHTEST record patch reload failed FAIL\n");
                  ok = false;
               }
            }
         }
      }
   }

   remove(path.c_str());
   printf("%s\n", ok ? "PAULSTRETCHTEST OK" : "PAULSTRETCHTEST FAIL");
   return ok;
}

// ================================================= INFINITE_RESONATORTEST
bool RunResonatorFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int blockSize = 512;
   const float rootFreq = 440.0f;
   const float decay = 1.5f;

   const EffectDef* resDef = nullptr;
   for (const auto& def : GetEffectDefs())
   {
      if (def.name == "Resonator Bank")
      {
         resDef = &def;
         break;
      }
   }
   if (!resDef)
   {
      printf("RESONATORTEST Resonator Bank def not found FAIL\n");
      return false;
   }

   ResonatorBankKernel kernel;
   kernel.PrepareToPlay(sr, blockSize);

   AudioEffectNode node(*resDef);
   *node.ParamPtr("rootFreq") = rootFreq;
   *node.ParamPtr("structure") = 0.0f; // Harmonic
   *node.ParamPtr("poles") = 1.0f;     // Single pole at fundamental to measure clear T60 & peak
   *node.ParamPtr("decay") = decay;
   *node.ParamPtr("scatter") = 0.0f;
   *node.ParamPtr("spread") = 0.0f;
   *node.ParamPtr("analog") = 0.0f;
   node.mix = 1.0f;
   kernel.PushParams(node, sr);

   const int totalSamples = (int)(4.0 * sr); // 4 seconds = 192000 samples
   const int numBlocks = totalSamples / blockSize;
   std::vector<float> impulseResponse(totalSamples, 0.0f);

   std::vector<float> inL(blockSize, 0.0f), inR(blockSize, 0.0f);
   std::vector<float> outL(blockSize, 0.0f), outR(blockSize, 0.0f);
   float* inChans[2] = { inL.data(), inR.data() };
   float* outChans[2] = { outL.data(), outR.data() };
   AudioBuffer inBuf { inChans, 2, blockSize };
   AudioBuffer outBuf { outChans, 2, blockSize };

   // Sample 0 is impulse 1.0
   inL[0] = inR[0] = 1.0f;

   for (int b = 0; b < numBlocks; b++)
   {
      kernel.ProcessBlock(inBuf, nullptr, outBuf);
      for (int i = 0; i < blockSize; i++)
         impulseResponse[b * blockSize + i] = outL[i];
      if (b == 0)
         inL[0] = inR[0] = 0.0f;
   }

   // 1. FFT peak check (16384-point FFT)
   const int kFftSize = 16384;
   const int kLog2 = 14;
   PortableFft::RealFft fft;
   fft.Prepare(kLog2);
   std::vector<float> realBuf(kFftSize / 2), imagBuf(kFftSize / 2);
   fft.Forward(impulseResponse.data(), kLog2, realBuf.data(), imagBuf.data());

   int maxBin = 1;
   float maxMag = 0.0f;
   for (int k = 1; k < kFftSize / 2; k++)
   {
      const float mag = sqrtf(realBuf[k] * realBuf[k] + imagBuf[k] * imagBuf[k]);
      if (mag > maxMag)
      {
         maxMag = mag;
         maxBin = k;
      }
   }
   const float peakHz = (float)maxBin * (float)sr / (float)kFftSize;
   const float freqErr = std::fabs(peakHz - rootFreq);
   if (freqErr > 6.0f)
   {
      printf("RESONATORTEST peak frequency expected %.1f Hz got %.1f Hz (err %.1f Hz) FAIL\n",
             rootFreq, peakHz, freqErr);
      ok = false;
   }
   else
   {
      printf("RESONATORTEST peak frequency: %.1f Hz (expected %.1f Hz) OK\n", peakHz, rootFreq);
   }

   // 2. T60 decay measurement
   const int winSamples = 2048;
   const int t0_samp = (int)(0.05 * sr);
   const int t1_samp = (int)(0.55 * sr);
   const float dt = (float)(t1_samp - t0_samp) / (float)sr;

   double sumSq0 = 0.0, sumSq1 = 0.0;
   for (int i = 0; i < winSamples; i++)
   {
      sumSq0 += impulseResponse[t0_samp + i] * impulseResponse[t0_samp + i];
      sumSq1 += impulseResponse[t1_samp + i] * impulseResponse[t1_samp + i];
   }
   const double rms0 = sqrt(sumSq0 / winSamples);
   const double rms1 = sqrt(sumSq1 / winSamples);
   const double dbDrop = 20.0 * log10((rms1 + 1e-12) / (rms0 + 1e-12));
   const float measuredT60 = (float)(-60.0 * dt / dbDrop);
   const float t60ErrFrac = std::fabs(measuredT60 - decay) / decay;

   if (t60ErrFrac > 0.25f)
   {
      printf("RESONATORTEST T60 decay expected %.2fs got %.2fs (err %.1f%%) FAIL\n",
             decay, measuredT60, t60ErrFrac * 100.0f);
      ok = false;
   }
   else
   {
      printf("RESONATORTEST T60 decay: %.2fs (expected %.2fs, err %.1f%%) OK\n",
             measuredT60, decay, t60ErrFrac * 100.0f);
   }

   // 3. Multi-pole metallic + spread test: poles = 16, structure = Metallic, rootFreq = 2000, decay = 10, spread = 1
   {
      ResonatorBankKernel k16;
      k16.PrepareToPlay(sr, blockSize);

      AudioEffectNode n16(*resDef);
      *n16.ParamPtr("rootFreq") = 2000.0f;
      *n16.ParamPtr("structure") = 3.0f; // Metallic
      *n16.ParamPtr("poles") = 16.0f;
      *n16.ParamPtr("decay") = 10.0f;
      *n16.ParamPtr("scatter") = 0.5f;
      *n16.ParamPtr("spread") = 1.0f;
      *n16.ParamPtr("analog") = 0.0f;
      n16.mix = 1.0f;
      k16.PushParams(n16, sr);

      double sumSqL = 0.0, sumSqR = 0.0;
      float peakAbs = 0.0f;
      bool allFinite = true;

      inL[0] = inR[0] = 1.0f;
      for (int b = 0; b < numBlocks; b++)
      {
         k16.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
         {
            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i]))
               allFinite = false;
            peakAbs = std::max(peakAbs, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
            sumSqL += (double)outL[i] * (double)outL[i];
            sumSqR += (double)outR[i] * (double)outR[i];
         }
         if (b == 0)
            inL[0] = inR[0] = 0.0f;
      }

      const double rmsL = sqrt(sumSqL / totalSamples);
      const double rmsR = sqrt(sumSqR / totalSamples);
      const double spreadDiff = std::fabs(rmsL - rmsR) / (rmsL + rmsR + 1e-12);

      const bool m16Ok = allFinite && (peakAbs < 1.0f) && (spreadDiff > 0.01);
      if (!m16Ok)
      {
         printf("RESONATORTEST 16-pole metallic/spread test failed (finite=%d peak=%.4f spreadDiff=%.4f) FAIL\n",
                (int)allFinite, peakAbs, (float)spreadDiff);
         ok = false;
      }
      else
      {
         printf("RESONATORTEST 16-pole metallic/spread: peak=%.4f spreadDiff=%.4f OK\n", peakAbs, (float)spreadDiff);
      }
   }

   // 4. Frequency sweep smoothing test: sweep rootFreq 110->880 across 200 blocks at decay = 5
   {
      ResonatorBankKernel kSweep;
      kSweep.PrepareToPlay(sr, blockSize);

      AudioEffectNode nSweep(*resDef);
      *nSweep.ParamPtr("rootFreq") = 110.0f;
      *nSweep.ParamPtr("structure") = 0.0f;
      *nSweep.ParamPtr("poles") = 8.0f;
      *nSweep.ParamPtr("decay") = 5.0f;
      *nSweep.ParamPtr("scatter") = 0.0f;
      *nSweep.ParamPtr("spread") = 0.5f;
      nSweep.mix = 1.0f;
      kSweep.PushParams(nSweep, sr);

      const int kSweepBlocks = 200;
      std::vector<float> sweepOut(kSweepBlocks * blockSize);

      // Continuous excitation: band-limited tone
      for (int i = 0; i < blockSize; i++)
         inL[i] = inR[i] = sinf((float)i * 0.05f) * 0.1f;

      // Warm up filter so initial impulse response settles
      for (int b = 0; b < 10; b++)
         kSweep.ProcessBlock(inBuf, nullptr, outBuf);

      for (int b = 0; b < kSweepBlocks; b++)
      {
         const float f = 110.0f + (880.0f - 110.0f) * ((float)b / (float)(kSweepBlocks - 1));
         *nSweep.ParamPtr("rootFreq") = f;
         kSweep.PushParams(nSweep, sr);

         kSweep.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            sweepOut[b * blockSize + i] = outL[i];
      }

      double stepSum = 0.0;
      float maxStep = 0.0f;
      const int N = (int)sweepOut.size();
      for (int i = 1; i < N; i++)
      {
         const float step = std::fabs(sweepOut[i] - sweepOut[i - 1]);
         stepSum += step;
         maxStep = std::max(maxStep, step);
      }
      const double meanStep = stepSum / (N - 1);
      const bool sweepOk = (maxStep < 8.0f * (float)meanStep);

      if (!sweepOk)
      {
         printf("RESONATORTEST sweep smoothing: maxStep=%.6f meanStep=%.6f (ratio=%.2f > 8) FAIL\n",
                maxStep, (float)meanStep, maxStep / (float)meanStep);
         ok = false;
      }
      else
      {
         printf("RESONATORTEST sweep smoothing: maxStep=%.6f meanStep=%.6f (ratio=%.2f) OK\n",
                maxStep, (float)meanStep, maxStep / (float)(meanStep + 1e-12));
      }
   }

   printf("%s\n", ok ? "RESONATORTEST OK" : "RESONATORTEST FAIL");
   return ok;
}

// ================================================= INFINITE_METALLICDECAYTEST
// Regression fixture for bugfix/metallic-decay-time: the Metallic node's
// `decay` knob (main.cpp, Physical Acoustics section, "%.2f s") did not
// deliver the time it displayed - a hard pole ceiling capped every mode at
// ~2.9s regardless of the knob, a second independent amp envelope shortened
// the audible tail further, and the knob was silently rescaled per material.
// Renders each of the 8 material presets at both 44.1kHz and 48kHz, at
// decay knob values of 1s/4s/9s, and checks that the measured peak-to-(-60dB)
// time on the summed output is within +/-20% of the knob, and that the voice
// frees itself within 2x the knob value.
bool RunMetallicDecayFixture()
{
   using namespace MetallicDsp;
   bool ok = true;

   const double sampleRates[] = { 44100.0, 48000.0 };
   const float decayValues[] = { 1.0f, 4.0f, 9.0f };

   for (double sr : sampleRates)
   {
      for (int mat = 0; mat < (int)kNumMaterials; mat++)
      {
         for (float decayKnob : decayValues)
         {
            MetallicVoice voice;
            voice.Trigger(60, 1.0f, 0, 220.0f, 1.0f, decayKnob, 0.5f, mat, 0.0f, sr);

            // 1.2x the knob plus half a second of headroom is enough to hold
            // both rate-measurement windows below, and to catch the 2x-knob
            // free deadline within the same render.
            const int totalSamples = (int)(decayKnob * 1.2 * sr) + (int)(0.5 * sr);
            const int freeDeadline = (int)(decayKnob * 2.0 * sr);

            std::vector<float> mono((size_t)totalSamples);
            bool allFinite = true;
            int freeAtSample = -1;

            for (int i = 0; i < totalSamples; i++)
            {
               float outL = 0.0f, outR = 0.0f;
               voice.Process(outL, outR);
               if (!std::isfinite(outL) || !std::isfinite(outR))
                  allFinite = false;
               mono[(size_t)i] = 0.5f * (outL + outR);
               if (freeAtSample < 0 && !voice.active)
                  freeAtSample = i;
            }

            // Rate-extrapolated T60: a mallet strike's broadband click sums
            // constructively across all 12 modes into a peak far louder
            // than the settled ring, and the modes also beat against each
            // other for the first stretch (non-monotonic local dB slope),
            // so neither "time from peak" nor a naive two-point read is
            // reliable - a linear regression of dB vs time over several
            // points, taken after the beating settles and comfortably
            // before the voice frees itself (energy runs out fastest on
            // the shortest/brightest presets, well inside the 2x-knob
            // deadline below), averages out the local wobble.
            const int winSamples = std::max(64, (int)(0.02 * sr));
            auto rmsAt = [&](int center) -> double {
               int hi = std::min(totalSamples, center + winSamples / 2);
               int lo = std::max(0, hi - winSamples);
               double sumSq = 0.0;
               for (int i = lo; i < hi; i++)
                  sumSq += (double)mono[(size_t)i] * (double)mono[(size_t)i];
               return sqrt(sumSq / std::max(1, hi - lo));
            };

            const double freeTimeSec = (freeAtSample >= 0) ? (freeAtSample / sr) : (decayKnob * 2.0);
            const double regionStart = 0.30 * decayKnob;
            const double regionEnd = std::max(regionStart + 0.05, std::min(0.75 * decayKnob, freeTimeSec * 0.85));

            const int numPoints = 10;
            double sumT = 0.0, sumDb = 0.0, sumTT = 0.0, sumTDb = 0.0;
            int n = 0;
            for (int p = 0; p < numPoints; p++)
            {
               const double t = regionStart + (regionEnd - regionStart) * ((double)p / (double)(numPoints - 1));
               const int center = (int)(t * sr);
               if (center < 0 || center >= totalSamples) continue;
               const double rms = rmsAt(center);
               if (rms <= 1e-9) continue;
               const double db = 20.0 * log10(rms);
               sumT += t; sumDb += db; sumTT += t * t; sumTDb += t * db;
               n++;
            }
            // Least-squares slope (dB/sec) of the regression points, then
            // extrapolate to the -60dB crossing to get T60.
            const double denom = (double)n * sumTT - sumT * sumT;
            const double slope = (n >= 2 && std::fabs(denom) > 1e-9)
               ? ((double)n * sumTDb - sumT * sumDb) / denom
               : 0.0;
            const float measuredT60 = (slope < -0.01) ? (float)(-60.0 / slope) : 1e9f;

            const float errFrac = std::fabs(measuredT60 - decayKnob) / decayKnob;
            const bool freedInTime = freeAtSample >= 0 && freeAtSample <= freeDeadline;
            const bool caseOk = allFinite && errFrac <= 0.20f && freedInTime;

            if (!caseOk)
            {
               printf("METALLICDECAYTEST %s @ %.0fHz decay=%.1fs -> measured T60=%.2fs (err %.1f%%) "
                      "finite=%d freedInTime=%d FAIL\n",
                      MaterialName(mat), sr, decayKnob, measuredT60,
                      errFrac * 100.0f, (int)allFinite, (int)freedInTime);
               ok = false;
            }
            else
            {
               printf("METALLICDECAYTEST %s @ %.0fHz decay=%.1fs -> measured T60=%.2fs (err %.1f%%) OK\n",
                      MaterialName(mat), sr, decayKnob, measuredT60, errFrac * 100.0f);
            }
         }
      }
   }

   // Note-off should not collapse a long decay to a fixed short tail: with
   // the decay knob at 9s, release the voice immediately after the strike
   // and confirm the release-phase T60 still tracks the knob rather than the
   // old fixed 0.4s (releaseSec = max(0.4, effectiveDecay), MetallicNode.cpp).
   {
      const double sr = 44100.0;
      const float decayKnob = 9.0f;
      MetallicVoice voice;
      voice.Trigger(60, 1.0f, 0, 220.0f, 1.0f, decayKnob, 0.5f, kSteel, 0.0f, sr);
      voice.Release(sr, std::max(0.4f, decayKnob));

      const int totalSamples = (int)(decayKnob * 0.7 * sr);
      std::vector<float> mono((size_t)totalSamples);
      int freeAtSample = -1;
      for (int i = 0; i < totalSamples; i++)
      {
         float outL = 0.0f, outR = 0.0f;
         voice.Process(outL, outR);
         mono[(size_t)i] = 0.5f * (outL + outR);
         if (freeAtSample < 0 && !voice.active)
            freeAtSample = i;
      }

      const int winSamples = std::max(64, (int)(0.02 * sr));
      auto rmsAt = [&](int center) -> double {
         int hi = std::min(totalSamples, center + winSamples / 2);
         int lo = std::max(0, hi - winSamples);
         double sumSq = 0.0;
         for (int i = lo; i < hi; i++)
            sumSq += (double)mono[(size_t)i] * (double)mono[(size_t)i];
         return sqrt(sumSq / std::max(1, hi - lo));
      };
      const double freeTimeSec = (freeAtSample >= 0) ? (freeAtSample / sr) : (decayKnob * 0.7);
      const int t0samp = (int)(0.25 * decayKnob * sr);
      const int t1samp = (int)(std::min(0.6 * decayKnob, freeTimeSec * 0.85) * sr);
      const double rms0 = rmsAt(t0samp);
      const double rms1 = rmsAt(t1samp);
      const double dbDrop = 20.0 * log10((rms1 + 1e-12) / (rms0 + 1e-12));
      const double dt = (double)(t1samp - t0samp) / sr;
      const float measuredT60 = (dbDrop < -0.01) ? (float)(-60.0 * dt / dbDrop) : 1e9f;

      // The old behaviour hard-capped the released tail at ~0.3s regardless
      // of the knob. releaseSec == effectiveDecay reuses ComputeAmpDecay()'s
      // safety-net envelope, which contributes only a little extra loss on
      // top of the modal decay - require the released tail to stay well
      // above the old fixed cutoff and reasonably close to the knob.
      const bool releaseOk = measuredT60 >= decayKnob * 0.7f;
      if (!releaseOk)
      {
         printf("METALLICDECAYTEST note-off tail: decay=%.1fs released T60=%.2fs (want >= %.2fs) FAIL\n",
                decayKnob, measuredT60, decayKnob * 0.7f);
         ok = false;
      }
      else
      {
         printf("METALLICDECAYTEST note-off tail: decay=%.1fs released T60=%.2fs OK\n",
                decayKnob, measuredT60);
      }
   }

   printf("%s\n", ok ? "METALLICDECAYTEST OK" : "METALLICDECAYTEST FAIL");
   return ok;
}

// ================================================= INFINITE_CYCLESHAPERTEST
bool RunCycleShaperFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int blockSize = 512;

   const EffectDef* csDef = nullptr;
   for (const auto& def : GetEffectDefs())
   {
      if (def.name == "Cycle Shaper")
      {
         csDef = &def;
         break;
      }
   }
   if (!csDef)
   {
      printf("CYCLESHAPERTEST Cycle Shaper def not found FAIL\n");
      return false;
   }

   CycleShaperKernel kernel;
   kernel.PrepareToPlay(sr, blockSize);

   AudioEffectNode node(*csDef);
   *node.ParamPtr("waveform") = 1.0f; // Square
   *node.ParamPtr("threshold") = -36.0f;
   *node.ParamPtr("smooth") = 8.0f;
   *node.ParamPtr("analog") = 0.0f;
   node.mix = 1.0f;
   kernel.PushParams(node, sr);

   std::vector<float> inL(blockSize, 0.0f), inR(blockSize, 0.0f);
   std::vector<float> outL(blockSize, 0.0f), outR(blockSize, 0.0f);
   float* inChans[2] = { inL.data(), inR.data() };
   float* outChans[2] = { outL.data(), outR.data() };
   AudioBuffer inBuf { inChans, 2, blockSize };
   AudioBuffer outBuf { outChans, 2, blockSize };

   // 1. 200 Hz sine at -6 dBFS -> Square wave periodicity and step test
   {
      const float freq = 200.0f;
      const float amp = 0.501187f; // -6 dBFS
      const int numBlocks = (int)(1.0 * sr / blockSize);
      std::vector<float> rec(numBlocks * blockSize);

      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float t = (float)(b * blockSize + i) / (float)sr;
            inL[i] = amp * sinf(2.0f * (float)M_PI * freq * t);
            inR[i] = inL[i];
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            rec[b * blockSize + i] = outL[i];
      }

      // Autocorrelation to find period in settled window [0.2s .. 0.8s]
      const int startSamp = (int)(0.2 * sr);
      const int lenSamp = (int)(0.5 * sr);
      int bestLag = 0;
      double bestCorr = -1.0;
      const int minLag = (int)(sr / 250.0); // ~192
      const int maxLag = (int)(sr / 160.0); // ~300

      for (int lag = minLag; lag <= maxLag; lag++)
      {
         double dot = 0.0, e0 = 0.0, e1 = 0.0;
         for (int i = 0; i < lenSamp; i++)
         {
            const double s0 = rec[startSamp + i];
            const double s1 = rec[startSamp + i + lag];
            dot += s0 * s1;
            e0 += s0 * s0;
            e1 += s1 * s1;
         }
         const double norm = sqrt(e0 * e1 + 1e-12);
         const double r = dot / norm;
         if (r > bestCorr)
         {
            bestCorr = r;
            bestLag = lag;
         }
      }

      const float measuredFreq = (float)sr / (float)bestLag;
      const float freqErr = std::fabs(measuredFreq - freq);
      if (freqErr > 2.0f || bestCorr < 0.95)
      {
         printf("CYCLESHAPERTEST 200 Hz square period: measured=%.2f Hz corr=%.4f (want 200 Hz +-2) FAIL\n",
                measuredFreq, bestCorr);
         ok = false;
      }
      else
      {
         printf("CYCLESHAPERTEST 200 Hz square period: %.2f Hz corr=%.4f OK\n", measuredFreq, bestCorr);
      }

      // Check max step vs mean step
      double stepSum = 0.0;
      float maxStep = 0.0f;
      for (int i = startSamp; i < startSamp + lenSamp; i++)
      {
         const float step = std::fabs(rec[i] - rec[i - 1]);
         stepSum += step;
         maxStep = std::max(maxStep, step);
      }
      const double meanStep = stepSum / (double)lenSamp;
      // For a 200 Hz square wave at 48 kHz (period 240 samples), with smooth=8 Hermite crossfade,
      // maximum step is gentle (transition spread across ~8 samples instead of a 2*A 1-sample jump).
      if (maxStep > 4.0f)
      {
         printf("CYCLESHAPERTEST max step too high (maxStep=%.4f) FAIL\n", maxStep);
         ok = false;
      }
      else
      {
         printf("CYCLESHAPERTEST smooth crossfade maxStep=%.4f meanStep=%.4f OK\n", maxStep, (float)meanStep);
      }
   }

   // 2. DC input at 0.5 for 1 second: output finite, no overrun
   {
      kernel.Reset();
      bool allFinite = true;
      for (int b = 0; b < (int)(1.0 * sr / blockSize); b++)
      {
         for (int i = 0; i < blockSize; i++)
            inL[i] = inR[i] = 0.5f;
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
         {
            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i]))
               allFinite = false;
         }
      }
      if (!allFinite)
      {
         printf("CYCLESHAPERTEST DC test non-finite FAIL\n");
         ok = false;
      }
      else
      {
         printf("CYCLESHAPERTEST DC 0.5 input finite and bounded OK\n");
      }
   }

   // 3. Silence for 1 second: output exactly zero
   {
      kernel.Reset();
      float maxSilenceOut = 0.0f;
      for (int b = 0; b < (int)(1.0 * sr / blockSize); b++)
      {
         for (int i = 0; i < blockSize; i++)
            inL[i] = inR[i] = 0.0f;
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
         {
            maxSilenceOut = std::max(maxSilenceOut, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
         }
      }
      if (maxSilenceOut > 1e-7f)
      {
         printf("CYCLESHAPERTEST silence output max=%.6f FAIL\n", maxSilenceOut);
         ok = false;
      }
      else
      {
         printf("CYCLESHAPERTEST silence output exactly zero OK\n");
      }
   }

   // 4. L fed 200 Hz, R fed 317 Hz: cross-correlation < 0.2
   {
      kernel.Reset();
      const int numBlocks = (int)(0.5 * sr / blockSize);
      std::vector<float> recL(numBlocks * blockSize), recR(numBlocks * blockSize);

      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float t = (float)(b * blockSize + i) / (float)sr;
            inL[i] = 0.5f * sinf(2.0f * (float)M_PI * 200.0f * t);
            inR[i] = 0.5f * sinf(2.0f * (float)M_PI * 317.0f * t);
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
         {
            recL[b * blockSize + i] = outL[i];
            recR[b * blockSize + i] = outR[i];
         }
      }

      double dot = 0.0, eL = 0.0, eR = 0.0;
      const int N = (int)recL.size();
      for (int i = (int)(0.1 * sr); i < N; i++)
      {
         dot += (double)recL[i] * (double)recR[i];
         eL += (double)recL[i] * (double)recL[i];
         eR += (double)recR[i] * (double)recR[i];
      }
      const double xcorr = std::fabs(dot) / (sqrt(eL * eR) + 1e-12);
      if (xcorr >= 0.20)
      {
         printf("CYCLESHAPERTEST L/R cross-correlation=%.4f (want < 0.20) FAIL\n", (float)xcorr);
         ok = false;
      }
      else
      {
         printf("CYCLESHAPERTEST per-channel L/R cross-correlation=%.4f OK\n", (float)xcorr);
      }
   }

   // 5. Input at -70 dBFS with threshold = -36 dBFS: passes through untouched (delayed by 1 cycle)
   {
      kernel.Reset();
      *node.ParamPtr("threshold") = -36.0f;
      kernel.PushParams(node, sr);

      const float amp70 = powf(10.0f, -70.0f / 20.0f);
      const float freq = 200.0f;
      const int numBlocks = (int)(0.5 * sr / blockSize);
      std::vector<float> inRec(numBlocks * blockSize), outRec(numBlocks * blockSize);

      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float t = (float)(b * blockSize + i) / (float)sr;
            inL[i] = inR[i] = amp70 * sinf(2.0f * (float)M_PI * freq * t);
            inRec[b * blockSize + i] = inL[i];
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outRec[b * blockSize + i] = outL[i];
      }

      // One wavecycle at 200 Hz is exactly 240 samples. Output should match input shifted by 240 samples,
      // plus the crossing estimator's look-ahead.
      const int cycleLen = (int)(sr / freq); // 240
      float maxDiff = 0.0f;
      const int startTest = (int)(0.1 * sr);
      const int endTest = (int)(0.4 * sr);
      for (int i = startTest; i < endTest; i++)
      {
         const float diff = std::fabs(outRec[i] - inRec[i - cycleLen - kernel.LatencySamples()]);
         maxDiff = std::max(maxDiff, diff);
      }

      if (maxDiff > 1e-5f)
      {
         printf("CYCLESHAPERTEST -70 dBFS passthrough max diff=%.8f FAIL\n", maxDiff);
         ok = false;
      }
      else
      {
         printf("CYCLESHAPERTEST -70 dBFS sub-threshold passthrough diff=%.2e OK\n", maxDiff);
      }
   }

   printf("%s\n", ok ? "CYCLESHAPERTEST OK" : "CYCLESHAPERTEST FAIL");
   return ok;
}

// ============================================= INFINITE_SPECTRUMSLIDETEST
// Two sines: slide 0 keeps the first, slide 1 gives the second, and in between
// the peak sits at the interpolated frequency (not two half-level peaks).
bool RunMidiFileFixture()
{
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      printf("MIDIFILETEST %s %s\n", what, cond ? "OK" : "FAIL");
      ok = ok && cond;
   };
   // Format 0, 480 ticks per beat: C4 beat 0 for 1 beat, E4 beat 1 for 1 beat, G4 beat 2 for 2 beats
   // (written with running status and a note-on-velocity-0 as the last off).
   const uint8_t trk[] = {
      0x00, 0x90, 60, 100,  0x83, 0x60, 60, 0,  0x00, 64, 100,  0x83, 0x60, 0x80, 64, 0,
      0x00, 0x90, 67, 90,   0x87, 0x40, 67, 0,  0x00, 0xFF, 0x2F, 0x00 };
   std::vector<uint8_t> bytes = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k',
                                  0, 0, 0, (uint8_t)sizeof(trk) };
   bytes.insert(bytes.end(), trk, trk + sizeof(trk));
   MidiFile::Song song;
   std::string err;
   check(MidiFile::Parse(bytes.data(), bytes.size(), song, err), "parse");
   check(song.noteCount == 3 && song.trackCount == 1, "3 notes in 1 track");
   check(std::fabs(song.lengthBeats - 4.0) < 1e-6, "length 4 beats");
   check(!MidiFile::Parse(bytes.data(), 10, song, err), "truncated header rejected");
   // SMPTE 25 fps x 40 subframes = 1000 ticks/s: a note at tick 1000 (1 s) sits at beat 2.
   {
      const uint8_t strk[] = { 0x87, 0x68, 0x90, 60, 100, 0x87, 0x68, 60, 0, 0x00, 0xFF, 0x2F, 0x00 }; // 1000, +1000
      std::vector<uint8_t> sm = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0xE7, 0x28, 'M', 'T', 'r', 'k', 0, 0, 0, (uint8_t)sizeof(strk) };
      sm.insert(sm.end(), strk, strk + sizeof(strk));
      MidiFile::Song ss;
      check(MidiFile::Parse(sm.data(), sm.size(), ss, err) && std::fabs(ss.events[0].beat - 2.0) < 1e-6, "SMPTE maps 1 s to beat 2");
      // Same bytes inside a RIFF/RMID wrapper and behind junk still load.
      std::vector<uint8_t> wrapped = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'R', 'M', 'I', 'D', 'd', 'a', 't', 'a', 0, 0, 0, 0 };
      wrapped.insert(wrapped.end(), bytes.begin(), bytes.end());
      MidiFile::Song ws;
      check(MidiFile::Parse(wrapped.data(), wrapped.size(), ws, err) && ws.noteCount == 3, "RMID-wrapped file loads");
   }
   // Format 2 loads like format 1.
   {
      std::vector<uint8_t> f2 = bytes;
      f2[9] = 2;
      MidiFile::Song s2;
      check(MidiFile::Parse(f2.data(), f2.size(), s2, err) && s2.noteCount == 3, "format 2 loads");
   }
   // A note-on with no note-off is closed at the end of the file.
   {
      const uint8_t otrk[] = { 0x00, 0x90, 60, 100, 0x83, 0x60, 0x90, 62, 100, 0x00, 0xFF, 0x2F, 0x00 };
      std::vector<uint8_t> om = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k', 0, 0, 0, (uint8_t)sizeof(otrk) };
      om.insert(om.end(), otrk, otrk + sizeof(otrk));
      MidiFile::Song os;
      check(MidiFile::Parse(om.data(), om.size(), os, err) && os.events.size() == 4 && !os.events[2].on && !os.events[3].on, "unclosed notes get an off");
   }

   // The system temp folder: "/tmp" does not exist on Windows.
   const std::filesystem::path file = std::filesystem::temp_directory_path() / "infinite_midifiletest.mid";
   const std::string path = file.u8string();
   {
      std::ofstream f(file, std::ios::binary);
      f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
   }
   MidiFileNode node;
   node.path = path;
   node.transpose = 12;
   node.CookIfNeeded(1);
   check(node.GetSong() != nullptr && node.Status().empty(), "node loads path on cook");

   const int sr = 48000, block = 512;
   AudioNode* an = node.GetAudioNode();
   an->PrepareToPlay((double)sr, block);
   node.CookIfNeeded(2);
   NoteEventQueue* out = an->NoteOutbox();
   const int cur = out->RegisterConsumer();
   Transport::Instance().SetTempo(120.0f);
   Transport::Instance().SeekBeats(0.0);
   Transport::Instance().NotifyAudioEngineStarted((double)sr);
   Transport::Instance().SetPlaying(true);

   struct Got { double beat; int note; bool on; };
   std::vector<Got> got;
   auto run = [&](int blocks) {
      for (int b = 0; b < blocks; b++)
      {
         Transport::Instance().AdvanceAudioClock(block);
         const double start = Transport::Instance().BlockStartBeats();
         std::vector<float> l(block, 0.0f);
         float* ch[1] = { l.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 1;
         buf.numFrames = block;
         an->ProcessBlock(nullptr, 0, buf);
         NoteEvent ev[32];
         const int n = out->Pop(cur, ev, 32);
         for (int i = 0; i < n; i++)
            got.push_back({ start + (double)ev[i].frameOffset / (double)block * (block * 2.0 / sr), ev[i].note, ev[i].isNoteOn });
      }
   };
   // 120 bpm: 2 beats/s = 96000 samples/4 beats... 4 beats = 2 s = 187.5 blocks. Run 3 s (one loop + 2 beats).
   run((int)(3.0 * sr / block));
   auto count = [&](int note, bool on) { int c = 0; for (auto& g : got) c += (g.note == note && g.on == on); return c; };
   check(count(72, true) >= 2 && count(76, true) >= 1 && count(79, true) >= 1, "transposed notes fired (looped C twice)");
   check(count(72, true) == count(72, false) || count(72, true) == count(72, false) + 1, "offs pair with ons");
   bool timingOk = false;
   for (auto& g : got)
      if (g.note == 76 && g.on && std::fabs(g.beat - 1.0) < 0.03)
         timingOk = true;
   check(timingOk, "E lands on beat 1");

   // Stop mid-note: held notes must be released.
   got.clear();
   Transport::Instance().SeekBeats(0.0);
   run(100); // 2.1 beats in: the G is sounding
   const int gOn = count(79, true);
   Transport::Instance().SeekBeats(0.5);
   run(1);
   check(gOn == 1 && count(79, false) >= 1, "seek releases the held G");
   // position 0.5 of a 4-beat file = beat 2: the G now sounds at transport beat 0.
   got.clear();
   node.position = 0.5f;
   node.CookIfNeeded(3);
   Transport::Instance().SeekBeats(0.0);
   run(2);
   check(count(79, true) == 1 && count(72, true) == 0, "position shifts the playhead into the file");
   // Speed 2: the file's beat 1 (the E) arrives at transport beat 0.5, independent of the project bpm.
   got.clear();
   node.position = 0.0f;
   node.speed = 2.0f;
   node.CookIfNeeded(4);
   Transport::Instance().SeekBeats(0.0);
   run(30);
   bool fast = false;
   for (auto& g : got)
      if (g.note == 76 && g.on && std::fabs(g.beat - 0.5) < 0.03)
         fast = true;
   check(fast, "speed 2 plays the file twice as fast");
   // Global Scale: transpose +1 makes C# F G#; in C major every note that sounds is in the scale, and each off
   // releases the pitch its on played even when the key changes while it is held.
   {
      const int key0 = Transport::Instance().Key(), scale0 = Transport::Instance().Scale();
      Transport::Instance().SetKey(0);
      Transport::Instance().SetScale(MusicTime::kMajor);
      Transport::Instance().SetPlaying(false); // flush what the speed test left sounding
      run(1);
      Transport::Instance().SetPlaying(true);
      got.clear();
      node.speed = 1.0f;
      node.transpose = 1;
      node.useGlobalScale = true;
      node.CookIfNeeded(5);
      Transport::Instance().SeekBeats(0.0);
      run(100); // 2.1 beats: C#->C/D and F have played, G# is held
      Transport::Instance().SetKey(1); // the key moves under the held note
      run(100);
      Transport::Instance().SetPlaying(false);
      run(1);
      bool inScale = true;
      int early = 0; // ons played while the key was still C major (C#, F, G# at beats 0, 1, 2)
      int balance[128] = {};
      for (auto& g : got)
      {
         if (g.on && g.beat < 2.05)
         {
            early++;
            inScale = inScale && MusicTime::SnapToScale(g.note, 0, MusicTime::kMajor, MusicTime::kSnapNearest) == g.note;
         }
         balance[g.note] += g.on ? 1 : -1;
      }
      bool paired = true;
      for (int b : balance)
         paired = paired && b == 0;
      check(early == 3 && inScale, "Global Scale snaps the file's notes into the key");
      check(paired, "snapped offs release the pitch their ons played");
      node.useGlobalScale = false;
      node.transpose = 12;
      node.CookIfNeeded(6);
      Transport::Instance().SetKey(key0);
      Transport::Instance().SetScale(scale0);
   }
   Transport::Instance().SetPlaying(false);
   Transport::Instance().NotifyAudioEngineStopped();
   std::remove(path.c_str());
   printf("MIDIFILETEST %s\n", ok ? "ALL OK" : "FAILED");
   return ok;
}

// ============================================== INFINITE_MPETEST
// MPE input (R597): each held note follows its own member channel's pitch bend,
// the master bend adds to every note, MPE off leaves ordinary keyboards alone,
// and note pressure / slide only exist while an MPE node is listening.
bool RunMpeFixture()
{
   bool ok = true;
   auto check = [&](bool cond, const char* what) {
      printf("MPETEST %s %s\n", what, cond ? "OK" : "FAIL");
      ok = ok && cond;
   };
   std::string err;
   Platform::MidiStart(err);
   const Platform::MidiDeviceId dev = 5150;
   auto feed = [&](std::initializer_list<unsigned char> bytes) {
      std::vector<unsigned char> b(bytes);
      Platform::MidiInjectBytes(b.data(), b.size(), dev);
   };

   MidiNotesNode node;
   node.mpe = true;
   node.mpeBendRange = 48.0f;
   int frame = 1;
   node.CookIfNeeded(frame++);
   AudioNode* audio = node.GetAudioNode();
   audio->PrepareToPlay(48000.0, 64);
   NoteEventQueue* q = audio->NoteOutbox();
   q->ResetConsumers();
   const int cursor = q->RegisterConsumer();
   float l[64] = {}, r[64] = {};
   float* chans[2] = { l, r };
   AudioBuffer out { chans, 2, 64 };
   NoteEvent ev[64];
   auto run = [&]() {
      audio->ProcessBlock(nullptr, 0, out);
      return q->Pop(cursor, ev, 64);
   };
   auto find = [&](int n, bool on, bool bendUpd, int note) -> const NoteEvent* {
      for (int i = 0; i < n; i++)
         if (ev[i].note == note && ev[i].isNoteOn == on && ev[i].bendUpdate == bendUpd)
            return &ev[i];
      return nullptr;
   };

   // Bend arrives before the note-on, as controllers send it: note 60 on
   // member channel 2 (index 1) at +half deflection starts at +24 st.
   feed({ 0xE1, 0x00, 0x60 });
   feed({ 0x91, 60, 100 });
   feed({ 0x92, 64, 100 }); // member channel 3, centred
   int n = run();
   const NoteEvent* on60 = find(n, true, false, 60);
   const NoteEvent* on64 = find(n, true, false, 64);
   check(on60 && std::fabs(on60->bendSemitones - 24.0f) < 0.05f, "note starts at its member channel's bend (+24 st)");
   check(on64 && std::fabs(on64->bendSemitones) < 0.05f, "second note on another channel starts unbent");
   check(on60 && on64 && on60->voiceId != on64->voiceId, "two voices");

   // Bending channel 3 moves only the note on channel 3.
   feed({ 0xE2, 0x00, 0x20 });
   n = run();
   const NoteEvent* up64 = find(n, false, true, 64);
   check(up64 && up64->voiceId == on64->voiceId && std::fabs(up64->bendSemitones + 24.0f) < 0.05f,
         "bend on channel 3 retunes note 64 to -24 st");
   check(find(n, false, true, 60) == nullptr, "note 60 is not touched by channel 3's bend");

   // A bend that does not move produces no event.
   check(run() == 0, "steady bend sends nothing");

   // Note-off on channel 3 does not release note 60; 60 keeps following channel 2.
   feed({ 0x82, 64, 0 });
   n = run();
   const NoteEvent* off64 = find(n, false, false, 64);
   check(off64 && off64->voiceId == on64->voiceId, "note-off closes note 64's voice");
   check(find(n, false, false, 60) == nullptr, "note 60 stays held");
   feed({ 0xE1, 0x00, 0x40 }); // channel 2 back to centre
   n = run();
   const NoteEvent* up60 = find(n, false, true, 60);
   check(up60 && up60->voiceId == on60->voiceId && std::fabs(up60->bendSemitones) < 0.05f, "note 60 follows channel 2 back to 0");

   // Master channel bend (+-2 st) adds to every note.
   feed({ 0xE0, 0x00, 0x60 });
   n = run();
   up60 = find(n, false, true, 60);
   check(up60 && std::fabs(up60->bendSemitones - 1.0f) < 0.05f, "master bend adds +1 st to every note");
   feed({ 0xE0, 0x00, 0x40 });
   run();

   // MPE off: a held bent note is straightened, and further member bends do nothing.
   feed({ 0xE1, 0x00, 0x60 });
   run();
   node.mpe = false;
   node.CookIfNeeded(frame++);
   n = run();
   up60 = find(n, false, true, 60);
   check(up60 && up60->bendSemitones == 0.0f, "switching MPE off straightens a held note");
   feed({ 0xE1, 0x00, 0x20 });
   check(run() == 0, "MPE off: member-channel bend sends no per-note update (ordinary keyboard unchanged)");
   feed({ 0x81, 60, 0 });
   run();

   // 2.15: each note carries its own channel's pressure and slide, from the
   // note-on (sent before it, as controllers do) and on every move.
   node.mpe = true;
   node.CookIfNeeded(frame++);
   run();
   feed({ 0xD3, 40 });
   feed({ 0xB3, 74, 100 });
   feed({ 0x93, 67, 100 });
   feed({ 0xD4, 120 });
   feed({ 0xB4, 74, 10 });
   feed({ 0x94, 70, 100 });
   n = run();
   const NoteEvent* on67 = find(n, true, false, 67);
   const NoteEvent* on70 = find(n, true, false, 70);
   check(on67 && on67->hasExpression && std::fabs(on67->pressure - 40.0f / 127.0f) < 0.005f
            && std::fabs(on67->slide - 100.0f / 127.0f) < 0.005f,
         "note-on carries its channel's pressure and slide");
   check(on70 && std::fabs(on70->pressure - 120.0f / 127.0f) < 0.005f && std::fabs(on70->slide - 10.0f / 127.0f) < 0.005f,
         "a second note on another channel carries its own");
   feed({ 0xD3, 90 });
   n = run();
   const NoteEvent* ex67 = find(n, false, true, 67);
   check(ex67 && ex67->hasExpression && ex67->voiceId == on67->voiceId && std::fabs(ex67->pressure - 90.0f / 127.0f) < 0.005f
            && std::fabs(ex67->slide - 100.0f / 127.0f) < 0.005f,
         "pressure on channel 4 updates note 67 only, slide unchanged");
   check(find(n, false, true, 70) == nullptr, "note 70 is not touched by channel 4's pressure");
   feed({ 0xB4, 74, 80 });
   n = run();
   const NoteEvent* ex70 = find(n, false, true, 70);
   check(ex70 && std::fabs(ex70->slide - 80.0f / 127.0f) < 0.005f && find(n, false, true, 67) == nullptr,
         "slide on channel 5 updates note 70 only");
   check(run() == 0, "steady pressure and slide send nothing");
   node.mpe = false;
   node.CookIfNeeded(frame++);
   n = run();
   ex67 = find(n, false, true, 67);
   check(ex67 && ex67->pressure == 0.0f && ex67->slide == 0.0f, "switching MPE off releases a held note's pressure and slide");
   feed({ 0xD3, 10 });
   check(run() == 0, "MPE off: pressure sends no per-note update");
   feed({ 0x83, 67, 0 });
   feed({ 0x84, 70, 0 });
   run();

   // Learn: the next note-on on any input becomes the node's channel; the
   // device id comes with it (its name is what saves).
   node.BeginLearn();
   node.PollLearn();
   check(node.IsLearning(), "Learn waits for a key");
   feed({ 0x95, 52, 90 });
   node.PollLearn();
   check(!node.IsLearning() && node.channel == 5, "Learn takes the channel of the next key played");
   feed({ 0x85, 52, 0 });
   run();

   // Pressure and slide exist only while an MPE node listens.
   const Platform::MidiDeviceId dev2 = 5151;
   auto feed2 = [&](std::initializer_list<unsigned char> bytes) {
      std::vector<unsigned char> b(bytes);
      Platform::MidiInjectBytes(b.data(), b.size(), dev2);
   };
   float v = -1.0f;
   feed2({ 0xD2, 100 });
   check(!Platform::MidiRead(dev2, 0, Platform::kMidiControllerNotePressure, false, v), "MPE off: no note-pressure controller");
   node.mpe = true;
   node.CookIfNeeded(frame++);
   feed2({ 0xD2, 127 });
   feed2({ 0xB3, 74, 64 });
   check(Platform::MidiRead(dev2, 0, Platform::kMidiControllerNotePressure, false, v) && std::fabs(v - 1.0f) < 0.01f,
         "MPE on: member-channel pressure reads as Note Pressure");
   check(Platform::MidiRead(dev2, 0, Platform::kMidiControllerNoteSlide, false, v) && std::fabs(v - 64.0f / 127.0f) < 0.01f,
         "MPE on: member-channel CC74 reads as Note Slide");
   feed2({ 0xD0, 50 }); // master channel pressure is not a note's
   check(Platform::MidiRead(dev2, 0, Platform::kMidiControllerNotePressure, false, v) && std::fabs(v - 1.0f) < 0.01f,
         "master-channel pressure does not move Note Pressure");

   // Param round trip is covered by ROUNDTRIPTEST and AUDIOPARAMSWEEPTEST.
   check(Platform::MidiBindingName(false, Platform::kMidiControllerNoteSlide) == "Note Slide", "binding name");

   printf("MPETEST %s\n", ok ? "ALL OK" : "FAILED");
   return ok;
}

bool RunShapeResonatorFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int blockSize = 512;

   const EffectDef* def = nullptr;
   for (const auto& d : GetEffectDefs())
      if (d.name == "Shape Resonator")
         def = &d;
   if (!def)
   {
      printf("SHAPERESONATORTEST def not found FAIL\n");
      return false;
   }

   auto energyAt = [&](const std::vector<float>& x, double hz) {
      double re = 0.0, im = 0.0;
      const int n = (int)x.size();
      for (int i = 0; i < n; i++)
      {
         const double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n - 1)));
         const double ph = 2.0 * M_PI * hz * (double)i / sr;
         re += w * x[i] * cos(ph);
         im += w * x[i] * sin(ph);
      }
      return (re * re + im * im) / ((double)n * (double)n);
   };
   auto dB = [](double a, double b) { return 10.0 * log10((a + 1e-20) / (b + 1e-20)); };

   // One impulse in, one second of ring out. `geo` null = the default plate.
   auto ring = [&](IGeometrySource* geo, float tune, float pos, std::vector<float>& rec, int* modeCount, float decay = 2.0f,
                   float damping = 0.0f, float modes = 16.0f) {
      ShapeResonatorKernel kernel;
      kernel.PrepareToPlay(sr, blockSize);
      AudioEffectNode node(*def);
      node.geometry = geo;
      *node.ParamPtr("tune") = tune;
      *node.ParamPtr("decay") = decay;
      *node.ParamPtr("damping") = damping;
      *node.ParamPtr("pos") = pos;
      *node.ParamPtr("modes") = modes;
      node.mix = 1.0f;
      for (int tries = 0; tries < 400 && kernel.SolvedModeCount() == 0; tries++)
      {
         kernel.PushParams(node, sr);
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      kernel.PushParams(node, sr);
      if (modeCount)
         *modeCount = kernel.SolvedModeCount();

      std::vector<float> iL(blockSize, 0.0f), iR(blockSize, 0.0f), oL(blockSize), oR(blockSize);
      float* iCh[2] = { iL.data(), iR.data() };
      float* oCh[2] = { oL.data(), oR.data() };
      AudioBuffer inBuf { iCh, 2, blockSize };
      AudioBuffer outBuf { oCh, 2, blockSize };
      const int numBlocks = (int)(1.0 * sr / blockSize);
      rec.assign((size_t)numBlocks * blockSize, 0.0f);
      for (int b = 0; b < numBlocks; b++)
      {
         std::fill(iL.begin(), iL.end(), 0.0f);
         std::fill(iR.begin(), iR.end(), 0.0f);
         if (b == 0)
            iL[0] = iR[0] = 1.0f;
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            rec[(size_t)b * blockSize + i] = oL[i];
      }
   };

   // 1. Default plate (1.5 x 1, free edges): first mode, then (0,1) at 1.5x.
   {
      std::vector<float> rec;
      int modes = 0;
      ring(nullptr, 200.0f, 0.1f, rec, &modes);
      const double e200 = energyAt(rec, 200.0);
      const double e283 = energyAt(rec, 300.0);
      const double off = energyAt(rec, 240.0);
      const double m1 = dB(e200, off), m2 = dB(e283, off);
      const bool pass = modes >= 8 && m1 > 15.0 && m2 > 15.0;
      printf("SHAPERESONATORTEST plate: %d modes, 200 Hz %.1f dB and 300 Hz %.1f dB over 240 Hz %s\n", modes, m1, m2,
             pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 2. Strike position: at the node line of the first mode it must not speak.
   {
      std::vector<float> a, b;
      ring(nullptr, 200.0f, 0.02f, a, nullptr);
      ring(nullptr, 200.0f, 0.5f, b, nullptr);
      const double d = dB(energyAt(a, 200.0), energyAt(b, 200.0));
      const bool pass = d > 15.0;
      printf("SHAPERESONATORTEST strike: edge vs centre at the first mode %.1f dB %s\n", d, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 3. A real geometry input: a sphere's first modes (l = 1) sit at
   //    tune * sqrt(6 / 2) = 1.73 x tune, and there is a ringing set.
   {
      GeometryNode sphere;
      const auto& names = GeometryNode::ShapeNames();
      for (size_t i = 0; i < names.size(); i++)
         if (names[i] == "Sphere" || names[i] == "sphere")
            sphere.shape = (int)i;
      std::vector<float> rec;
      int modes = 0;
      ring(&sphere, 200.0f, 0.3f, rec, &modes);
      const double e = energyAt(rec, 346.4);
      const double off = energyAt(rec, 300.0);
      const double m = dB(e, off);
      const bool pass = modes >= 8 && m > 10.0;
      printf("SHAPERESONATORTEST sphere: %d modes, l=1 triplet at 346 Hz %.1f dB over 300 Hz %s\n", modes, m,
             pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 4. Level: an impulse must not blow up or vanish.
   {
      std::vector<float> rec;
      ring(nullptr, 200.0f, 0.1f, rec, nullptr);
      float peak = 0.0f;
      for (float v : rec)
         peak = std::max(peak, std::fabs(v));
      const bool pass = peak > 0.01f && peak < 4.0f && std::isfinite(peak);
      printf("SHAPERESONATORTEST level: impulse peak %.3f %s\n", peak, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 5. decay, damping and modes each change the ring (the generic param sweep
   //    cannot see them: the solve is asynchronous, so its short rig hears silence).
   {
      std::vector<float> a, b, c, d, e;
      ring(nullptr, 200.0f, 0.1f, a, nullptr, 0.3f);
      ring(nullptr, 200.0f, 0.1f, b, nullptr, 3.0f);
      const double dDecay = dB(energyAt(b, 200.0), energyAt(a, 200.0));
      ring(nullptr, 200.0f, 0.1f, c, nullptr, 2.0f, 1.0f);
      ring(nullptr, 200.0f, 0.1f, d, nullptr, 2.0f, 0.0f);
      // damping shortens the high modes: compare the 300 Hz mode, 1.5 x the tune
      const double dDamp = dB(energyAt(d, 300.0), energyAt(c, 300.0));
      ring(nullptr, 200.0f, 0.1f, e, nullptr, 2.0f, 0.0f, 1.0f);
      const double dModes = dB(energyAt(d, 300.0), energyAt(e, 300.0));
      const bool pass = dDecay > 6.0 && dDamp > 3.0 && dModes > 10.0;
      printf("SHAPERESONATORTEST params: decay %.1f dB, damping %.1f dB, modes %.1f dB %s\n", dDecay, dDamp, dModes,
             pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 6. Curves (R616): a free-ended string rings the harmonic series, lambda_n = (n pi / L)^2, so the
   //    frequency ratios are 1:2:3:4 whatever the point spacing. A closed curve is a ring: modes in pairs.
   {
      const float line[6] = { 0, 0, 0, 3, 0, 0 }; // two points only: resampled internally
      const auto m = MeshModalSolver::SolveCurve(line, 2, false, 8);
      bool pass = m.valid && m.count >= 4;
      double r[4] = {};
      for (int k = 0; pass && k < 4; k++)
      {
         r[k] = std::sqrt(m.lambda[k] / m.lambda[0]);
         pass = std::fabs(r[k] - (double)(k + 1)) < 0.03 * (k + 1);
      }
      printf("SHAPERESONATORTEST string: ratios %.3f %.3f %.3f %.3f (want 1 2 3 4) %s\n", r[0], r[1], r[2], r[3],
             pass ? "OK" : "FAIL");
      ok = ok && pass;

      std::vector<float> circle;
      for (int i = 0; i < 24; i++)
         circle.insert(circle.end(), { cosf(6.2831853f * i / 24), sinf(6.2831853f * i / 24), 0.0f });
      const auto ring2 = MeshModalSolver::SolveCurve(circle.data(), 24, true, 8);
      const bool pairs = ring2.valid && ring2.count >= 4 && std::fabs(ring2.lambda[1] / ring2.lambda[0] - 1.0) < 0.05 &&
                         std::fabs(ring2.lambda[2] / ring2.lambda[0] - 4.0) < 0.2 &&
                         std::fabs(ring2.lambda[3] / ring2.lambda[0] - 4.0) < 0.2;
      printf("SHAPERESONATORTEST ring: lambda ratios 1 : %.3f : %.3f : %.3f (want 1 1 4 4) %s\n",
             ring2.count >= 4 ? ring2.lambda[1] / ring2.lambda[0] : 0.0, ring2.count >= 4 ? ring2.lambda[2] / ring2.lambda[0] : 0.0,
             ring2.count >= 4 ? ring2.lambda[3] / ring2.lambda[0] : 0.0, pairs ? "OK" : "FAIL");
      ok = ok && pairs;
   }

   // 7. Point clouds (R616): a grid of points rings an ascending, finite, positive set; two separate clusters
   //    still solve (each is its own rigid body, so the zero modes are skipped).
   {
      std::vector<float> grid, twin;
      for (int j = 0; j < 12; j++)
         for (int i = 0; i < 18; i++)
            grid.insert(grid.end(), { (float)i, (float)j, 0.0f });
      for (int j = 0; j < 6; j++)
         for (int i = 0; i < 6; i++)
         {
            twin.insert(twin.end(), { (float)i, (float)j, 0.0f });
            twin.insert(twin.end(), { (float)i + 40.0f, (float)j, 0.0f });
         }
      bool pass = true;
      for (const std::vector<float>* pts : { &grid, &twin })
      {
         const auto m = MeshModalSolver::SolveCloud(pts->data(), (int)(pts->size() / 3), 16);
         bool good = m.valid && m.count >= 8;
         for (int k = 0; good && k < m.count; k++)
            good = std::isfinite(m.lambda[k]) && m.lambda[k] > 0.0f && (k == 0 || m.lambda[k] >= m.lambda[k - 1]);
         printf("SHAPERESONATORTEST cloud: %zu points -> %d modes %s\n", pts->size() / 3, m.count, good ? "OK" : "FAIL");
         pass = pass && good;
      }
      ok = ok && pass;
   }

   // 8. Unusable input must come back invalid, never crash or hang: empty, one triangle, NaN, collinear
   //    (zero area), out-of-range indices, vertices without faces, too few points, a one-point curve.
   {
      const float tri[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
      const uint32_t triIdx[3] = { 0, 1, 2 };
      std::vector<float> nanMesh, flat, plateV;
      std::vector<uint32_t> plateI;
      for (int i = 0; i < 12; i++)
      {
         nanMesh.insert(nanMesh.end(), { (float)i, (float)(i % 3), 0.0f });
         flat.insert(flat.end(), { (float)i, 0.0f, 0.0f }); // collinear: every triangle has zero area
      }
      nanMesh[7] = std::nanf("");
      std::vector<uint32_t> fan;
      for (uint32_t i = 1; i + 1 < 12; i++)
         fan.insert(fan.end(), { 0, i, i + 1 });
      const std::vector<uint32_t> wild(fan.size(), 9999u);
      const float one[3] = { 1, 2, 3 };
      const float few[21] = {};
      int bad = 0;
      auto expectInvalid = [&](const char* what, const MeshModalSolver::Modes& m) {
         if (m.valid)
         {
            printf("SHAPERESONATORTEST degenerate: %s came back valid FAIL\n", what);
            bad++;
         }
      };
      expectInvalid("null mesh", MeshModalSolver::Solve(nullptr, 0, nullptr, 0, 8));
      expectInvalid("one triangle", MeshModalSolver::Solve(tri, 3, triIdx, 3, 8));
      expectInvalid("NaN vertex", MeshModalSolver::Solve(nanMesh.data(), 12, fan.data(), (int)fan.size(), 8));
      expectInvalid("zero-area mesh", MeshModalSolver::Solve(flat.data(), 12, fan.data(), (int)fan.size(), 8));
      expectInvalid("out-of-range indices", MeshModalSolver::Solve(flat.data(), 12, wild.data(), (int)wild.size(), 8));
      expectInvalid("seven-point cloud", MeshModalSolver::SolveCloud(few, 7, 8));
      expectInvalid("one-point curve", MeshModalSolver::SolveCurve(one, 1, false, 8));
      expectInvalid("NaN curve", MeshModalSolver::SolveCurve(nanMesh.data(), 12, false, 8));
      expectInvalid("coincident curve", MeshModalSolver::SolveCurve(few, 7, false, 8));

      // Two separate plates, and three triangles sharing one edge: must solve (or refuse) without crashing.
      std::vector<float> twoV;
      std::vector<uint32_t> twoI;
      for (int plate = 0; plate < 2; plate++)
         for (int j = 0; j <= 6; j++)
            for (int i = 0; i <= 6; i++)
               twoV.insert(twoV.end(), { (float)i + 20.0f * plate, (float)j, 0.0f });
      for (int plate = 0; plate < 2; plate++)
         for (int j = 0; j < 6; j++)
            for (int i = 0; i < 6; i++)
            {
               const uint32_t v = (uint32_t)(plate * 49 + j * 7 + i);
               twoI.insert(twoI.end(), { v, v + 1, v + 8, v, v + 8, v + 7 });
            }
      const auto two = MeshModalSolver::Solve(twoV.data(), (int)(twoV.size() / 3), twoI.data(), (int)twoI.size(), 8);
      if (!two.valid || two.count < 4)
      {
         printf("SHAPERESONATORTEST degenerate: two disconnected plates gave %d modes FAIL\n", two.count);
         bad++;
      }
      std::vector<float> fin = twoV;
      fin.insert(fin.end(), { 3.0f, 3.0f, 5.0f });
      std::vector<uint32_t> nm = twoI;
      for (uint32_t k = 0; k < 3; k++)
         nm.insert(nm.end(), { 24, 25, 98 + 0 * k });
      (void)MeshModalSolver::Solve(fin.data(), (int)(fin.size() / 3), nm.data(), (int)nm.size(), 8);
      printf("SHAPERESONATORTEST degenerate: %d unexpected valid, two plates %d modes %s\n", bad, two.count,
             bad == 0 ? "OK" : "FAIL");
      ok = ok && bad == 0;
   }

   // 9. End to end through the kernel with a source that is only a curve, and one that is only a cloud:
   //    both must ring (this is the path R616 added), and a source that goes empty must fall silent.
   {
      struct StubSource : IGeometrySource
      {
         Mesh mesh;
         std::vector<Particle> cloud;
         Polyline curve;
         bool hasCurve = false;
         unsigned long long rev = 1;
         const Mesh& GetMesh() override { return mesh; }
         unsigned long long MeshRevision() override { return rev; }
         Mat4 GetModelMatrix() const override { return Mat4::Identity(); }
         Material GetMaterial() const override { return Material(); }
         const std::vector<Particle>* GetPointCloud() override { return cloud.empty() ? nullptr : &cloud; }
         unsigned long long PointCloudRevision() override { return cloud.size(); }
         const Polyline* GetCurve() override { return hasCurve ? &curve : nullptr; }
         unsigned long long CurveStamp() override { return hasCurve ? 7 : 0; }
      };
      StubSource curveSrc, cloudSrc;
      curveSrc.hasCurve = true;
      curveSrc.curve.points = { 0, 0, 0, 2, 0, 0, 2, 1, 0 };
      for (int j = 0; j < 8; j++)
         for (int i = 0; i < 10; i++)
         {
            Particle pt;
            pt.px = (float)i;
            pt.py = (float)j;
            cloudSrc.cloud.push_back(pt);
         }
      std::vector<float> a, b;
      int ma = 0, mb = 0;
      ring(&curveSrc, 200.0f, 0.3f, a, &ma);
      ring(&cloudSrc, 200.0f, 0.3f, b, &mb);
      auto peakOf = [](const std::vector<float>& v) {
         float pk = 0.0f;
         for (float x : v)
            pk = std::max(pk, std::fabs(x));
         return pk;
      };
      const bool pass = ma >= 4 && mb >= 4 && peakOf(a) > 0.01f && peakOf(b) > 0.01f && std::isfinite(peakOf(a)) &&
                        std::isfinite(peakOf(b));
      printf("SHAPERESONATORTEST sources: curve %d modes (peak %.3f), cloud %d modes (peak %.3f) %s\n", ma, peakOf(a), mb,
             peakOf(b), pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   printf("SHAPERESONATORTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

bool RunSpectrumSlideFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int blockSize = 512;

   const EffectDef* def = nullptr;
   for (const auto& d : GetEffectDefs())
      if (d.name == "Spectrum Slide")
         def = &d;
   if (!def)
   {
      printf("SPECTRUMSLIDETEST def not found FAIL\n");
      return false;
   }

   auto energyAt = [&](const std::vector<float>& x, int start, int n, double hz) {
      double re = 0.0, im = 0.0;
      for (int i = 0; i < n; i++)
      {
         const double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n - 1)));
         const double ph = 2.0 * M_PI * hz * (double)(start + i) / sr;
         re += w * x[start + i] * cos(ph);
         im += w * x[start + i] * sin(ph);
      }
      return (re * re + im * im) / ((double)n * (double)n);
   };

   // fA / fB of 0 means "no tone"; wireB false leaves the second input uncabled.
   auto run = [&](float slide, double fA, double fB, bool wireB, std::vector<float>& outRec) {
      SpectrumSlideKernel kernel;
      kernel.PrepareToPlay(sr, blockSize);
      AudioEffectNode node(*def);
      *node.ParamPtr("slide") = slide;
      node.mix = 1.0f;
      kernel.PushParams(node, sr);

      std::vector<float> aL(blockSize), aR(blockSize), bL(blockSize), bR(blockSize), oL(blockSize), oR(blockSize);
      float* aCh[2] = { aL.data(), aR.data() };
      float* bCh[2] = { bL.data(), bR.data() };
      float* oCh[2] = { oL.data(), oR.data() };
      AudioBuffer aBuf { aCh, 2, blockSize };
      AudioBuffer bBuf { bCh, 2, blockSize };
      AudioBuffer oBuf { oCh, 2, blockSize };

      const int numBlocks = (int)(1.5 * sr / blockSize);
      outRec.assign((size_t)numBlocks * blockSize, 0.0f);
      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const double t = (double)(b * blockSize + i) / sr;
            aL[i] = aR[i] = fA > 0.0 ? (float)(0.3 * sin(2.0 * M_PI * fA * t)) : 0.0f;
            bL[i] = bR[i] = fB > 0.0 ? (float)(0.3 * sin(2.0 * M_PI * fB * t)) : 0.0f;
         }
         kernel.ProcessBlock(aBuf, wireB ? &bBuf : nullptr, oBuf);
         for (int i = 0; i < blockSize; i++)
            outRec[(size_t)b * blockSize + i] = oL[i];
      }
   };

   const int start = (int)(0.5 * sr);
   const int len = (int)(0.9 * sr);
   auto dB = [](double a, double b) { return 10.0 * log10((a + 1e-20) / (b + 1e-20)); };

   // 1. Endpoints: slide 0 is A, slide 1 is B.
   for (int endpoint = 0; endpoint < 2; endpoint++)
   {
      std::vector<float> out;
      run((float)endpoint, 440.0, 880.0, true, out);
      const double want = endpoint ? 880.0 : 440.0;
      const double other = endpoint ? 440.0 : 880.0;
      const double margin = dB(energyAt(out, start, len, want), energyAt(out, start, len, other));
      printf("SPECTRUMSLIDETEST slide %d -> %.0f Hz: %.1f dB over %.0f %s\n", endpoint, want, margin, other,
             margin > 20.0 ? "OK" : "FAIL");
      ok = ok && margin > 20.0;
   }

   // 2. Midpoint: 440 -> 880 at 0.5 lands on 660 Hz, well above both endpoints.
   {
      std::vector<float> out;
      run(0.5f, 440.0, 880.0, true, out);
      const double e660 = energyAt(out, start, len, 660.0);
      const double m1 = dB(e660, energyAt(out, start, len, 440.0));
      const double m2 = dB(e660, energyAt(out, start, len, 880.0));
      const bool pass = m1 > 15.0 && m2 > 15.0;
      printf("SPECTRUMSLIDETEST midpoint 660 Hz: %.1f / %.1f dB over the endpoints %s\n", m1, m2, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 3. A quarter of the way: 440 -> 880 at 0.25 lands on 550 Hz.
   {
      std::vector<float> out;
      run(0.25f, 440.0, 880.0, true, out);
      const double e550 = energyAt(out, start, len, 550.0);
      const double m1 = dB(e550, energyAt(out, start, len, 440.0));
      const double m2 = dB(e550, energyAt(out, start, len, 880.0));
      const bool pass = m1 > 12.0 && m2 > 12.0;
      printf("SPECTRUMSLIDETEST quarter 550 Hz: %.1f / %.1f dB over the endpoints %s\n", m1, m2, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 4. Level: a morph keeps the sound's loudness. Slide 0 must match the
   //    input, and mid-slide may lose at most 4 dB against it (the moved lobe
   //    is not the ideal window lobe, so overlap-add cancels a little).
   {
      auto rmsAt = [&](float slide) {
         std::vector<float> out;
         run(slide, 440.0, 880.0, true, out);
         double acc = 0.0;
         for (int i = 0; i < len; i++)
            acc += (double)out[start + i] * out[start + i];
         return sqrt(acc / len);
      };
      const double rmsIn = 0.3 * sqrt(0.5);
      const double d0 = fabs(20.0 * log10(rmsAt(0.0f) / rmsIn));
      const double dMid = 20.0 * log10(rmsAt(0.5f) / rmsIn);
      const bool pass = d0 < 0.5 && dMid > -4.0 && dMid < 1.0;
      printf("SPECTRUMSLIDETEST level: slide 0 %.2f dB off, midpoint %.2f dB %s\n", d0, dMid, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 5. Nothing on 'to': the sound passes through unchanged in pitch.
   {
      std::vector<float> out;
      run(1.0f, 440.0, 880.0, false, out);
      const double margin = dB(energyAt(out, start, len, 440.0), energyAt(out, start, len, 880.0));
      printf("SPECTRUMSLIDETEST uncabled 'to' keeps 440 Hz: %.1f dB %s\n", margin, margin > 20.0 ? "OK" : "FAIL");
      ok = ok && margin > 20.0;
   }

   // 6. Silent 'to': a fade, not a pitch drag - the peak stays at 440 Hz.
   {
      std::vector<float> out;
      run(0.5f, 440.0, 0.0, true, out);
      const double margin = dB(energyAt(out, start, len, 440.0), energyAt(out, start, len, 330.0));
      printf("SPECTRUMSLIDETEST silent 'to' keeps 440 Hz: %.1f dB %s\n", margin, margin > 20.0 ? "OK" : "FAIL");
      ok = ok && margin > 20.0;
   }

   printf("%s\n", ok ? "SPECTRUMSLIDETEST OK" : "SPECTRUMSLIDETEST FAIL");
   return ok;
}

// ==================================================== INFINITE_KEYSNAPTEST
// Sines that sit between notes must land on the nearest note of the scale,
// every peak independently; snap 0 must leave them where they are.
bool RunKeySnapFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int blockSize = 512;

   const EffectDef* def = nullptr;
   for (const auto& d : GetEffectDefs())
      if (d.name == "Key-Snap")
         def = &d;
   if (!def)
   {
      printf("KEYSNAPTEST Key-Snap def not found FAIL\n");
      return false;
   }

   // Energy of `x` at `hz` over `n` samples (Hann-windowed Goertzel).
   auto energyAt = [&](const std::vector<float>& x, int start, int n, double hz) {
      double re = 0.0, im = 0.0;
      for (int i = 0; i < n; i++)
      {
         const double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(n - 1)));
         const double ph = 2.0 * M_PI * hz * (double)(start + i) / sr;
         re += w * x[start + i] * cos(ph);
         im += w * x[start + i] * sin(ph);
      }
      return (re * re + im * im) / ((double)n * (double)n);
   };

   auto run = [&](float snap, float glide, int scale, int root, const std::vector<double>& freqs, std::vector<float>& outRec) {
      KeySnapKernel kernel;
      kernel.PrepareToPlay(sr, blockSize);
      AudioEffectNode node(*def);
      *node.ParamPtr("snap") = snap;
      *node.ParamPtr("glide") = glide;
      *node.ParamPtr("scale") = (float)scale;
      *node.ParamPtr("root") = (float)root;
      node.mix = 1.0f;
      kernel.PushParams(node, sr);

      std::vector<float> inL(blockSize), inR(blockSize), outL(blockSize), outR(blockSize);
      float* inChans[2] = { inL.data(), inR.data() };
      float* outChans[2] = { outL.data(), outR.data() };
      AudioBuffer inBuf { inChans, 2, blockSize };
      AudioBuffer outBuf { outChans, 2, blockSize };

      const int numBlocks = (int)(1.5 * sr / blockSize);
      outRec.assign((size_t)numBlocks * blockSize, 0.0f);
      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const double t = (double)(b * blockSize + i) / sr;
            double v = 0.0;
            for (double f : freqs)
               v += 0.25 * sin(2.0 * M_PI * f * t);
            inL[i] = inR[i] = (float)v;
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outRec[(size_t)b * blockSize + i] = outL[i];
      }
   };

   const int start = (int)(0.5 * sr);
   const int len = (int)(0.9 * sr);
   auto dB = [](double a, double b) { return 10.0 * log10((a + 1e-20) / (b + 1e-20)); };

   // 1. One sine at 450 Hz (A4 + 39 cents) in C major -> 440 Hz.
   {
      std::vector<float> out;
      run(1.0f, 0.0f, MusicTime::kMajor, 0, { 450.0 }, out);
      const double eT = energyAt(out, start, len, 440.0);
      const double eO = energyAt(out, start, len, 450.0);
      const double margin = dB(eT, eO);
      printf("KEYSNAPTEST single 450->440 Hz: target %.1f dB over original %s\n", margin, margin > 20.0 ? "OK" : "FAIL");
      ok = ok && margin > 20.0;
   }

   // 2. Two sines, 450 and 530 Hz, in C major -> 440 and 523.25 Hz (polyphonic).
   {
      std::vector<float> out;
      run(1.0f, 0.0f, MusicTime::kMajor, 0, { 450.0, 530.0 }, out);
      const double m1 = dB(energyAt(out, start, len, 440.0), energyAt(out, start, len, 450.0));
      const double m2 = dB(energyAt(out, start, len, 523.25), energyAt(out, start, len, 530.0));
      const bool pass = m1 > 20.0 && m2 > 20.0;
      printf("KEYSNAPTEST chord 450+530: margins %.1f / %.1f dB %s\n", m1, m2, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   // 3. snap 0 leaves the sine alone.
   {
      std::vector<float> out;
      run(0.0f, 0.0f, MusicTime::kMajor, 0, { 450.0 }, out);
      const double margin = dB(energyAt(out, start, len, 450.0), energyAt(out, start, len, 440.0));
      printf("KEYSNAPTEST snap 0 keeps 450 Hz: %.1f dB over 440 %s\n", margin, margin > 20.0 ? "OK" : "FAIL");
      ok = ok && margin > 20.0;
   }

   // 4. Level survives: output RMS within 3 dB of the input's.
   {
      std::vector<float> out;
      run(1.0f, 0.0f, MusicTime::kMajor, 0, { 450.0, 530.0 }, out);
      double acc = 0.0;
      for (int i = 0; i < len; i++)
         acc += (double)out[start + i] * out[start + i];
      const double rmsOut = sqrt(acc / len);
      const double rmsIn = 0.25 * sqrt(2.0 * 0.5); // two sines of amplitude 0.25
      const double diff = fabs(20.0 * log10(rmsOut / rmsIn));
      printf("KEYSNAPTEST level: out %.4f in %.4f (%.2f dB) %s\n", rmsOut, rmsIn, diff, diff < 3.0 ? "OK" : "FAIL");
      ok = ok && diff < 3.0;
   }

   // 5. Scale lookup: 450 Hz in A minor pentatonic -> A (440), not B/C.
   {
      const uint32_t mask = KeySnapKernel::ScaleMask(MusicTime::kMinorPentatonic, 9);
      const float midi = 69.0f + 12.0f * log2f(450.0f / 440.0f);
      const float snapped = KeySnapKernel::NearestScaleNote(midi, mask);
      const bool pass = snapped == 69.0f;
      printf("KEYSNAPTEST scale lookup 450 Hz in A minor pent -> midi %.0f %s\n", snapped, pass ? "OK" : "FAIL");
      ok = ok && pass;
   }

   printf("%s\n", ok ? "KEYSNAPTEST OK" : "KEYSNAPTEST FAIL");
   return ok;
}

// ==================================================== INFINITE_SPECBLURTEST
bool RunSpecBlurFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int blockSize = 512;
   const int N = SpecBlurKernel::kFftSize; // 2048

   const EffectDef* sbDef = nullptr;
   for (const auto& def : GetEffectDefs())
   {
      if (def.name == "Spec Blur")
      {
         sbDef = &def;
         break;
      }
   }
   if (!sbDef)
   {
      printf("SPECBLURTEST Spec Blur def not found FAIL\n");
      return false;
   }

   SpecBlurKernel kernel;
   kernel.PrepareToPlay(sr, blockSize);

   AudioEffectNode node(*sbDef);
   *node.ParamPtr("blurTime") = 300.0f;
   *node.ParamPtr("tilt") = 0.0f;
   *node.ParamPtr("diffusion") = 0.0f;
   *node.ParamPtr("freeze") = 0.0f;
   *node.ParamPtr("analog") = 0.0f;
   node.mix = 1.0f;
   kernel.PushParams(node, sr);

   std::vector<float> inL(blockSize, 0.0f), inR(blockSize, 0.0f);
   std::vector<float> outL(blockSize, 0.0f), outR(blockSize, 0.0f);
   float* inChans[2] = { inL.data(), inR.data() };
   float* outChans[2] = { outL.data(), outR.data() };
   AudioBuffer inBuf { inChans, 2, blockSize };
   AudioBuffer outBuf { outChans, 2, blockSize };

   // 1. Framing round-trip test (white noise, pure Hann OLA analysis/synthesis)
   {
      kernel.Reset();
      kernel.SetPassthroughOnly(true);

      const int numBlocks = 64;
      const int totalSamples = numBlocks * blockSize;
      std::vector<float> inRec(totalSamples), outRec(totalSamples);

      uint32_t rng = 0x9e3779b9u;
      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float v = ((float)(rng & 0x00FFFFFFu) / 8388608.0f) - 1.0f;
            inL[i] = inR[i] = v * 0.5f;
            inRec[b * blockSize + i] = inL[i];
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outRec[b * blockSize + i] = outL[i];
      }

      // Compare settled region after startup latency (from 2*N to total-N)
      const int startComp = 2 * N;
      const int endComp = totalSamples - N;
      double errEnergy = 0.0, sigEnergy = 0.0;
      for (int i = startComp; i < endComp; i++)
      {
         const double diff = outRec[i] - inRec[i - N];
         errEnergy += diff * diff;
         sigEnergy += (double)inRec[i - N] * (double)inRec[i - N];
      }
      const double relRmsErr = sqrt(errEnergy / (sigEnergy + 1e-12));
      if (relRmsErr > 1e-3)
      {
         printf("SPECBLURTEST framing round-trip rel RMS err=%.6f (want < 1e-3) FAIL\n", (float)relRmsErr);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST framing round-trip rel RMS err=%.2e OK\n", (float)relRmsErr);
      }
      kernel.SetPassthroughOnly(false);
   }

   // 2. Impulse latency test
   {
      kernel.Reset();
      kernel.SetPassthroughOnly(true);

      const int numBlocks = 32;
      std::vector<float> outRec(numBlocks * blockSize);
      const int impulseIndex = 1024; // within settled region

      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const int sampleIdx = b * blockSize + i;
            inL[i] = inR[i] = (sampleIdx == impulseIndex) ? 1.0f : 0.0f;
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outRec[b * blockSize + i] = outL[i];
      }

      int peakIdx = -1;
      float peakVal = 0.0f;
      for (size_t i = 0; i < outRec.size(); i++)
      {
         if (std::fabs(outRec[i]) > peakVal)
         {
            peakVal = std::fabs(outRec[i]);
            peakIdx = (int)i;
         }
      }

      const int expectedPeak = impulseIndex + N;
      const int latencyErr = std::abs(peakIdx - expectedPeak);
      if (latencyErr > 1 || peakVal < 0.5f)
      {
         printf("SPECBLURTEST impulse latency: peak at %d (want %d +-1, peakVal=%.4f) FAIL\n",
                peakIdx, expectedPeak, peakVal);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST impulse latency: peak at sample %d (expected %d, peakVal=%.4f) OK\n",
                peakIdx, expectedPeak, peakVal);
      }
      kernel.SetPassthroughOnly(false);
   }

   // 3. DC and Nyquist test
   {
      kernel.Reset();
      // Test DC response with fast blur time (10ms) so filter settles rapidly
      *node.ParamPtr("blurTime") = 10.0f;
      *node.ParamPtr("diffusion") = 0.0f;
      *node.ParamPtr("freeze") = 0.0f;
      kernel.PushParams(node, sr);

      const int numBlocks = 64;
      std::vector<float> outDc(numBlocks * blockSize), outNyq(numBlocks * blockSize);

      // DC input 0.5
      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
            inL[i] = inR[i] = 0.5f;
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outDc[b * blockSize + i] = outL[i];
      }

      // Check settled DC (after 8*N warmup)
      float minDc = 100.0f, maxDc = -100.0f;
      for (int i = 8 * N; i < (int)outDc.size(); i++)
      {
         minDc = std::min(minDc, outDc[i]);
         maxDc = std::max(maxDc, outDc[i]);
      }
      const float dcRipple = maxDc - minDc;
      if (dcRipple > 0.05f || maxDc < 0.45f || minDc > 0.55f)
      {
         printf("SPECBLURTEST DC response: min=%.4f max=%.4f ripple=%.4f FAIL\n", minDc, maxDc, dcRipple);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST DC response: stable at %.4f (ripple=%.4f) OK\n", (minDc + maxDc) * 0.5f, dcRipple);
      }

      // Nyquist input alternating +0.5 / -0.5
      kernel.Reset();
      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
            inL[i] = inR[i] = ((i % 2) == 0 ? 0.5f : -0.5f);
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outNyq[b * blockSize + i] = outL[i];
      }

      // Check that Nyquist doesn't leak into DC offset
      double nyqDcSum = 0.0;
      for (int i = 8 * N; i < (int)outNyq.size(); i++)
         nyqDcSum += outNyq[i];
      const double nyqDcMean = std::fabs(nyqDcSum / (outNyq.size() - 8 * N));
      if (nyqDcMean > 0.02)
      {
         printf("SPECBLURTEST Nyquist DC leak=%.4f FAIL\n", (float)nyqDcMean);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST Nyquist response: DC leakage=%.2e OK\n", (float)nyqDcMean);
      }
   }

   // 4. Blur tests:
   // a) Fast blur (10ms, diffusion=0): correlation with delayed input > 0.99
   {
      kernel.Reset();
      *node.ParamPtr("blurTime") = 10.0f;
      *node.ParamPtr("tilt") = 0.0f;
      *node.ParamPtr("diffusion") = 0.0f;
      *node.ParamPtr("freeze") = 0.0f;
      kernel.PushParams(node, sr);

      const int numBlocks = 64;
      std::vector<float> inRec(numBlocks * blockSize), outRec(numBlocks * blockSize);
      for (int b = 0; b < numBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float t = (float)(b * blockSize + i) / (float)sr;
            inL[i] = inR[i] = 0.4f * sinf(2.0f * (float)M_PI * 440.0f * t) + 0.2f * sinf(2.0f * (float)M_PI * 880.0f * t);
            inRec[b * blockSize + i] = inL[i];
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         for (int i = 0; i < blockSize; i++)
            outRec[b * blockSize + i] = outL[i];
      }

      double dot = 0.0, e0 = 0.0, e1 = 0.0;
      for (int i = 4 * N; i < (int)inRec.size() - N; i++)
      {
         const double s0 = inRec[i - N];
         const double s1 = outRec[i];
         dot += s0 * s1;
         e0 += s0 * s0;
         e1 += s1 * s1;
      }
      const double corr = dot / (sqrt(e0 * e1) + 1e-12);
      if (corr < 0.95)
      {
         printf("SPECBLURTEST fast blur corr=%.4f (want > 0.95) FAIL\n", (float)corr);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST fast blur (10ms) corr=%.4f OK\n", (float)corr);
      }
   }

   // b) Freeze test: freeze=1, mute input after 1s, RMS stays within +-2 dB over 3s, finite
   {
      kernel.Reset();
      *node.ParamPtr("blurTime") = 200.0f;
      *node.ParamPtr("diffusion") = 0.0f;
      *node.ParamPtr("freeze") = 0.0f;
      kernel.PushParams(node, sr);

      const int blocks1s = (int)(1.0 * sr / blockSize);
      for (int b = 0; b < blocks1s; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const float t = (float)(b * blockSize + i) / (float)sr;
            inL[i] = inR[i] = 0.5f * sinf(2.0f * (float)M_PI * 330.0f * t);
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
      }

      // Freeze on and feed silence
      *node.ParamPtr("freeze") = 1.0f;
      kernel.PushParams(node, sr);

      for (int i = 0; i < blockSize; i++)
         inL[i] = inR[i] = 0.0f;

      // Measure RMS of frozen tail across 3 seconds
      const int freezeBlocks = (int)(3.0 * sr / blockSize);
      std::vector<float> freezeEnergy(freezeBlocks, 0.0f);
      bool allFinite = true;

      for (int b = 0; b < freezeBlocks; b++)
      {
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         float sumSq = 0.0f;
         for (int i = 0; i < blockSize; i++)
         {
            if (!std::isfinite(outL[i])) allFinite = false;
            sumSq += outL[i] * outL[i];
         }
         freezeEnergy[b] = sqrtf(sumSq / (float)blockSize);
      }

      const float eStart = freezeEnergy[8]; // after pipeline clears input
      const float eEnd = freezeEnergy[freezeBlocks - 1];
      const float dropDb = 20.0f * log10f(std::max(eEnd, 1e-6f) / std::max(eStart, 1e-6f));

      if (!allFinite || std::fabs(dropDb) > 3.0f || eStart < 0.05f)
      {
         printf("SPECBLURTEST freeze: eStart=%.4f eEnd=%.4f drop=%.2fdB finite=%d FAIL\n",
                eStart, eEnd, dropDb, allFinite);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST freeze: sustain drop=%.2fdB over 3s OK\n", dropDb);
      }
   }

   // c) 5000ms blur on pulse burst: decay < 3 dB over 1s
   {
      kernel.Reset();
      *node.ParamPtr("blurTime") = 5000.0f;
      *node.ParamPtr("diffusion") = 0.0f;
      *node.ParamPtr("freeze") = 0.0f;
      kernel.PushParams(node, sr);

      // Feed short burst (50ms of 440 Hz sine)
      const int burstSamples = (int)(0.05 * sr);
      const int totalBlocks = (int)(3.0 * sr / blockSize);
      std::vector<float> blockRms(totalBlocks, 0.0f);

      for (int b = 0; b < totalBlocks; b++)
      {
         for (int i = 0; i < blockSize; i++)
         {
            const int sampleIdx = b * blockSize + i;
            if (sampleIdx < burstSamples)
               inL[i] = inR[i] = 0.5f * sinf(2.0f * (float)M_PI * 440.0f * (float)sampleIdx / (float)sr);
            else
               inL[i] = inR[i] = 0.0f;
         }
         kernel.ProcessBlock(inBuf, nullptr, outBuf);
         float sumSq = 0.0f;
         for (int i = 0; i < blockSize; i++)
            sumSq += outL[i] * outL[i];
         blockRms[b] = sqrtf(sumSq / (float)blockSize);
      }

      // Sample energy at 0.5s and 1.5s (1s span during blur decay)
      const int idx05 = (int)(0.5 * sr / blockSize);
      const int idx15 = (int)(1.5 * sr / blockSize);
      const float r1 = blockRms[idx05];
      const float r2 = blockRms[idx15];
      const float decayDb = 20.0f * log10f(std::max(r2, 1e-6f) / std::max(r1, 1e-6f));

      if (std::fabs(decayDb) > 3.5f || r1 < 1e-5f)
      {
         printf("SPECBLURTEST 5000ms blur smear: r1=%.5f r2=%.5f decay=%.2fdB FAIL\n", r1, r2, decayDb);
         ok = false;
      }
      else
      {
         printf("SPECBLURTEST 5000ms blur smear: decay=%.2fdB over 1s OK\n", decayDb);
      }
   }

   printf("%s\n", ok ? "SPECBLURTEST OK" : "SPECBLURTEST FAIL");
   return ok;
}

// ================================================= INFINITE_GRAINMOLDERTEST
bool RunGrainMolderFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int len = (int)(2.0 * sr); // 2 seconds

   std::vector<float> input(len);
   for (int i = 0; i < len; i++)
   {
      const float t = (float)i / (float)sr;
      input[i] = 0.4f * sinf(2.0f * (float)M_PI * 220.0f * t) + 0.2f * sinf(2.0f * (float)M_PI * 440.0f * t);
   }

   // 1. amount = 0: output vs input relative RMS error below -40 dB
   {
      GrainMolderDsp::Params p;
      p.grainMs = 50.0f;
      p.amount = 0.0f;
      p.key = 0;
      std::vector<float> outL;
      GrainMolderDsp::Process(input.data(), len, sr, p, outL);

      double errEnergy = 0.0, sigEnergy = 0.0;
      const int hop = (int)(p.grainMs * sr / 2000.0);
      const int startComp = 2 * hop;
      const int endComp = len - 2 * hop;
      for (int i = startComp; i < endComp; i++)
      {
         const double diff = outL[i] - input[i];
         errEnergy += diff * diff;
         sigEnergy += (double)input[i] * (double)input[i];
      }
      const double relRms = sqrt(errEnergy / (sigEnergy + 1e-12));
      const double relRmsDb = 20.0 * log10(std::max(relRms, 1e-9));
      if (relRmsDb > -40.0)
      {
         printf("GRAINMOLDERTEST amount=0 rel RMS err=%.2fdB (want < -40dB) FAIL\n", (float)relRmsDb);
         ok = false;
      }
      else
      {
         printf("GRAINMOLDERTEST amount=0 rel RMS err=%.2fdB (< -40dB) OK\n", (float)relRmsDb);
      }
   }

   // 2. Bijection test at amount = 0, 0.5, 1.0
   {
      bool bijectionOk = true;
      for (float amt : { 0.0f, 0.5f, 1.0f })
      {
         for (int k = 0; k < 3; k++)
         {
            GrainMolderDsp::Params p;
            p.grainMs = 40.0f;
            p.amount = amt;
            p.key = k;
            p.seed = 12345;

            std::vector<float> outL;
            GrainMolderDsp::Process(input.data(), len, sr, p, outL);
            if (outL.size() != (size_t)len)
               bijectionOk = false;
            for (float s : outL)
               if (!std::isfinite(s)) bijectionOk = false;
         }
      }
      if (!bijectionOk)
      {
         printf("GRAINMOLDERTEST bijection across amounts and keys FAIL\n");
         ok = false;
      }
      else
      {
         printf("GRAINMOLDERTEST bijection across amounts (0.0, 0.5, 1.0) and all keys OK\n");
      }
   }

   // 3. Determinism test: same (seed, amount, key, grain) renders bit-identically
   {
      GrainMolderDsp::Params p;
      p.grainMs = 60.0f;
      p.amount = 0.75f;
      p.key = 2; // Random
      p.seed = 987654;

      std::vector<float> out1, out2;
      GrainMolderDsp::Process(input.data(), len, sr, p, out1);
      GrainMolderDsp::Process(input.data(), len, sr, p, out2);

      bool bitIdentical = (out1 == out2);
      if (!bitIdentical)
      {
         printf("GRAINMOLDERTEST determinism FAIL\n");
         ok = false;
      }
      else
      {
         printf("GRAINMOLDERTEST determinism (bit-identical renders) OK\n");
      }
   }

   // 4. Degenerate inputs: all-silent input and 3-sample input
   {
      GrainMolderDsp::Params p;
      p.grainMs = 50.0f;
      p.amount = 0.5f;

      std::vector<float> silent(len, 0.0f);
      std::vector<float> outSilent;
      GrainMolderDsp::Process(silent.data(), len, sr, p, outSilent);

      bool silentOk = (outSilent.size() == (size_t)len);
      for (float s : outSilent)
         if (s != 0.0f) silentOk = false;

      std::vector<float> tiny(3, 0.5f);
      std::vector<float> outTiny;
      GrainMolderDsp::Process(tiny.data(), 3, sr, p, outTiny);

      bool tinyOk = (outTiny.size() == 3 && outTiny[0] == 0.5f);

      if (!silentOk || !tinyOk)
      {
         printf("GRAINMOLDERTEST degenerate inputs (silent=%d, tiny=%d) FAIL\n", silentOk, tinyOk);
         ok = false;
      }
      else
      {
         printf("GRAINMOLDERTEST degenerate inputs (silence and 3-sample buffer) OK\n");
      }
   }

   // 5. Node lifecycle & parameter updates
   {
      GrainMolderNode node;
      node.grain = 30.0f;
      node.amount = 0.5f;
      for (int i = 0; i < 20; i++)
      {
         node.amount = (float)i / 20.0f;
         node.CookIfNeeded(i);
      }
      printf("GRAINMOLDERTEST node lifecycle & rapid parameter updates OK\n");
   }

   printf("%s\n", ok ? "GRAINMOLDERTEST OK" : "GRAINMOLDERTEST FAIL");
   return ok;
}

// ===================================================== INFINITE_MOLDERTEST
//
// Headless test of MolderDsp's engine directly (Analyze/Mutate/Render are
// pure functions with no INode/thread dependency, so this needs none of the
// worker-thread machinery MolderNode wraps around them). Six assertions per
// new-audio-node/SKILL.md and the design doc's "Tests" section.
bool RunMolderFixture()
{
   bool ok = true;
   const double sr = 44100.0;
   constexpr float kTwoPiT = 6.28318530717958647692f;

   auto synthSaw = [&](float freq, float seconds) {
      const int n = (int)(seconds * sr);
      std::vector<float> buf(n);
      for (int i = 0; i < n; i++)
      {
         const float phase = fmodf((float)i * freq / (float)sr, 1.0f);
         buf[i] = 2.0f * phase - 1.0f;
      }
      return buf;
   };
   auto synthSine = [&](float freq, float seconds) {
      const int n = (int)(seconds * sr);
      std::vector<float> buf(n);
      for (int i = 0; i < n; i++)
         buf[i] = sinf(kTwoPiT * freq * (float)i / (float)sr);
      return buf;
   };

   // ---- 1. pitch: sawtooth and a bass sine, both within 1% -------------
   {
      auto saw = synthSaw(220.0f, 2.0f);
      MolderDsp::Analysis a;
      MolderDsp::Analyze(saw.data(), (int)saw.size(), sr, a);
      const float err = a.valid ? fabsf(a.globalF0 - 220.0f) / 220.0f : 1.0f;
      if (!(a.valid && err < 0.01f))
      {
         printf("MOLDERTEST pitch saw220 got %.2f Hz FAIL\n", a.globalF0);
         ok = false;
      }

      // The autocorrelation trap: a broken detector reports ~1500Hz here.
      auto bass = synthSine(55.0f, 2.0f);
      MolderDsp::Analysis a2;
      MolderDsp::Analyze(bass.data(), (int)bass.size(), sr, a2);
      const float err2 = a2.valid ? fabsf(a2.globalF0 - 55.0f) / 55.0f : 1.0f;
      if (!(a2.valid && err2 < 0.01f))
      {
         printf("MOLDERTEST pitch sine55 got %.2f Hz FAIL\n", a2.globalF0);
         ok = false;
      }
   }

   // ---- 2. voicing gate: noise gated off, sawtooth sustain gated on ----
   {
      std::vector<float> noise(88200);
      uint32_t rngState = 12345u;
      for (float& v : noise)
      {
         rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
         v = ((float)(rngState & 0x00FFFFFFu) / 16777216.0f) * 2.0f - 1.0f;
      }
      MolderDsp::Analysis an;
      MolderDsp::Analyze(noise.data(), (int)noise.size(), sr, an);
      bool framesQuiet = true;
      for (float v : an.frameVoicing)
         if (v > 0.01f) framesQuiet = false;
      if (!(an.globalConfidence < 0.25f && framesQuiet))
      {
         printf("MOLDERTEST voicing white noise not gated (conf %.2f) FAIL\n", an.globalConfidence);
         ok = false;
      }

      // Noise burst through a highpass-ish differentiator with exponential
      // decay - a cheap stand-in for a hi-hat transient.
      std::vector<float> hat(8192, 0.0f);
      float prev = 0.0f;
      for (size_t i = 0; i < hat.size(); i++)
      {
         rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5;
         const float w = ((float)(rngState & 0x00FFFFFFu) / 16777216.0f) * 2.0f - 1.0f;
         const float hp = w - prev;
         prev = w;
         const float env = expf(-(float)i / 800.0f);
         hat[i] = hp * env;
      }
      MolderDsp::Analysis ah;
      MolderDsp::Analyze(hat.data(), (int)hat.size(), sr, ah);
      bool hatQuiet = true;
      for (float v : ah.frameVoicing)
         if (v > 0.25f) hatQuiet = false;
      if (!hatQuiet)
      {
         printf("MOLDERTEST voicing hat-like burst not gated FAIL\n");
         ok = false;
      }

      auto saw = synthSaw(220.0f, 2.0f);
      MolderDsp::Analysis as;
      MolderDsp::Analyze(saw.data(), (int)saw.size(), sr, as);
      const int mid = as.numFrames / 2;
      if (!(as.valid && mid >= 0 && mid < (int)as.frameVoicing.size() && as.frameVoicing[mid] > 0.5f))
      {
         printf("MOLDERTEST voicing saw sustain got %.2f FAIL\n",
                (as.valid && mid < (int)as.frameVoicing.size()) ? as.frameVoicing[mid] : -1.0f);
         ok = false;
      }
   }

   // ---- 3/4/6: reconstruction, silenced partials, no-aliasing ----------
   {
      // A short fade-in/out rather than an abrupt digital on/off: a real
      // sample has a physical attack, and short-time windowed analysis has
      // an inherent latency of roughly half an FFT window at a genuinely
      // instantaneous onset (frame 0's buffer is half real signal, half
      // silence, and correctly reports lower energy for it) - a hard
      // square-edge onset is not representative of what this node analyses
      // in practice.
      auto saw = synthSaw(220.0f, 1.0f);
      {
         const int fadeLen = std::min((int)saw.size() / 4, (int)(sr * 0.02));
         for (int i = 0; i < fadeLen; i++)
         {
            const float g = 0.5f - 0.5f * cosf(kTwoPiT * 0.5f * (float)i / (float)fadeLen);
            saw[i] *= g;
            saw[saw.size() - 1 - i] *= g;
         }
      }
      MolderDsp::Analysis a;
      MolderDsp::Analyze(saw.data(), (int)saw.size(), sr, a);

      if (!a.valid)
      {
         printf("MOLDERTEST analysis of reconstruction source invalid FAIL\n");
         ok = false;
      }
      else
      {
         auto RmsErrDb = [](const std::vector<float>& ref, const std::vector<float>& test) {
            double errSum = 0.0, refSum = 0.0;
            const int n = (int)std::min(ref.size(), test.size());
            for (int i = 0; i < n; i++)
            {
               const double d = (double)ref[i] - (double)test[i];
               errSum += d * d;
               refSum += (double)ref[i] * (double)ref[i];
            }
            return 10.0 * log10((errSum + 1e-12) / (refSum + 1e-12));
         };

         // 3. Baseline genome must reconstruct the source to < -20dB RMS error.
         // A dense 48-harmonic sawtooth (harmonics ~10 bins apart at
         // 220Hz/2048-pt) sat at -6.7dB for a long time; three analysis
         // bugs, not the additive model, held it there: the phase-vocoder
         // frequency diffed two different bins whenever trackedF0 jitter
         // moved a partial's nominal bin (+/-90Hz at a high harmonic), the
         // seed phase came from one FFT bin plus a fitted fractional-bin
         // correction (now a joint least-squares fit of all partials), and
         // Hann scalloping read off-bin partials up to 1.4dB low. With those
         // fixed it reads about -26dB (sustain -30dB, attack/release -20dB).
         MolderDsp::Genome baseline;
         std::vector<float> rendered;
         MolderDsp::Render(a, baseline, rendered);
         const double errDb = RmsErrDb(saw, rendered);
         if (!(errDb < -20.0))
         {
            printf("MOLDERTEST reconstruction error %.1f dB FAIL\n", errDb);
            ok = false;
         }
         else
            printf("MOLDERTEST reconstruction error %.1f dB OK\n", errDb);

         // 4. Every partial silenced -> render equals the residual alone,
         // within -40dB (compare against tonal-bank-off, residual-only render).
         MolderDsp::Genome silenced = baseline;
         for (int h = 0; h < MolderDsp::kMaxPartials; h++)
            silenced.partialAmp[h] = 0.0f;
         std::vector<float> silencedRender;
         MolderDsp::Render(a, silenced, silencedRender);
         std::vector<float> residualOnly((size_t)a.sourceLen, 0.0f);
         for (int i = 0; i < a.sourceLen; i++)
            residualOnly[i] = a.residualSteady[i] + a.residualTransient[i];
         const double residualErrDb = RmsErrDb(residualOnly, silencedRender);
         if (!(residualErrDb < -40.0))
         {
            printf("MOLDERTEST silenced-partials residual match %.1f dB FAIL\n", residualErrDb);
            ok = false;
         }

         // 6. +24st pitch shift must not alias: energy above 0.45*sr stays
         // well below the render's total energy. A one-pole highpass can't
         // isolate a band this close to Nyquist (its rolloff is only
         // 6dB/octave and the band is under an octave wide) - probe with a
         // small Goertzel bank instead, one bin per few hundred Hz across
         // [0.45*sr, 0.5*sr).
         MolderDsp::Genome shifted = baseline;
         shifted.pitchShiftSemitones = 24.0f;
         std::vector<float> shiftedRender;
         MolderDsp::Render(a, shifted, shiftedRender);
         auto GoertzelPower = [&](float freq) {
            const double w = kTwoPiT * freq / sr;
            const double coeff = 2.0 * cos(w);
            double s1 = 0.0, s2 = 0.0;
            for (float x : shiftedRender)
            {
               const double s0 = (double)x + coeff * s1 - s2;
               s2 = s1;
               s1 = s0;
            }
            return s1 * s1 + s2 * s2 - coeff * s1 * s2;
         };
         double aboveEnergy = 0.0;
         constexpr int kProbes = 8;
         for (int p = 0; p < kProbes; p++)
            aboveEnergy += GoertzelPower((float)(0.46 + 0.035 * p) * (float)sr);
         double totalEnergy = 0.0;
         for (float s : shiftedRender) totalEnergy += (double)s * s;
         totalEnergy *= (double)shiftedRender.size() / (double)kProbes; // put both sums on comparable per-bin scale
         const double aboveDb = 10.0 * log10((aboveEnergy + 1e-15) / (totalEnergy + 1e-15));
         if (!(aboveDb < -20.0))
         {
            printf("MOLDERTEST pitch+24 above-band energy %.1f dB FAIL\n", aboveDb);
            ok = false;
         }
      }
   }

   // ---- 5. determinism: same (seed, generation) -> bit-identical genome -
   {
      auto saw = synthSaw(220.0f, 0.5f);
      MolderDsp::Analysis a;
      MolderDsp::Analyze(saw.data(), (int)saw.size(), sr, a);

      auto replay = [](uint32_t seed, int generation, float chaos) {
         MolderDsp::Genome g;
         MolderDsp::Rng rng(seed);
         for (int i = 0; i < generation; i++)
            MolderDsp::Mutate(g, chaos, rng);
         return g;
      };

      const MolderDsp::Genome g1 = replay(4242u, 7, 0.6f);
      const MolderDsp::Genome g2 = replay(4242u, 7, 0.6f);
      // Field-by-field, not memcmp: Genome ends in a trailing `bool`, so the
      // struct carries 3 bytes of compiler-inserted padding after it that no
      // constructor or Mutate() ever writes. Two independently-constructed
      // Genomes are logically identical but can differ in that uninitialized
      // padding, which a raw memcmp would misreport as a determinism bug.
      bool identical =
         memcmp(g1.partialAmp, g2.partialAmp, sizeof(g1.partialAmp)) == 0 &&
         memcmp(g1.bandAmp, g2.bandAmp, sizeof(g1.bandAmp)) == 0 &&
         g1.noiseAmount == g2.noiseAmount &&
         g1.transientAmount == g2.transientAmount &&
         g1.tonalAmount == g2.tonalAmount &&
         g1.attackScale == g2.attackScale &&
         g1.decayScale == g2.decayScale &&
         g1.decayTilt == g2.decayTilt &&
         g1.brightnessTilt == g2.brightnessTilt &&
         g1.inharmonicity == g2.inharmonicity &&
         g1.harmonicStretch == g2.harmonicStretch &&
         g1.pitchShiftSemitones == g2.pitchShiftSemitones &&
         g1.reverseResidual == g2.reverseResidual;
      if (!identical)
      {
         printf("MOLDERTEST determinism: same seed/generation diverged FAIL\n");
         ok = false;
      }

      // Save -> load -> replay must reproduce the same genome (MolderNode
      // only ever persists seed + generation, never the genome itself).
      MolderNode node;
      node.chaos = 0.6f;
      std::vector<std::pair<std::string, std::string>> params;
      Patch::SaveParams(&node, params);
      MolderNode reloaded;
      Patch::LoadParams(&reloaded, params);
      if (reloaded.Seed() != node.Seed() || reloaded.Generation() != node.Generation())
      {
         printf("MOLDERTEST save/load seed+generation mismatch FAIL\n");
         ok = false;
      }

      // Patch load, paste and undo/redo all go LoadParams -> ReloadFromPath().
      // The reload must keep the saved seed + generation (it used to re-roll a
      // random seed and reset to generation 0, so every reopen changed the
      // sound); only a fresh LoadFile() of a new source rolls a new seed.
      {
         const std::string wavPath = TmpPath("infinite_molder_reload_fixture.wav");
         {
            const int frames = 22050;
            std::vector<int16_t> pcm(frames);
            for (int i = 0; i < frames; ++i)
               pcm[i] = (int16_t)(std::sin((float)i * 220.0f * 6.283185f / 44100.0f) * 20000.0f);
            std::ofstream f(wavPath, std::ios::binary);
            auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
            auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
            const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
            f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
            f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
            writeU32(44100); writeU32(44100 * 2); writeU16(2); writeU16(16);
            f.write("data", 4); writeU32(dataSize);
            f.write((const char*)pcm.data(), dataSize);
         }
         std::vector<std::pair<std::string, std::string>> saved;
         Patch::SaveParams(&node, saved);
         for (auto& kv : saved)
         {
            if (kv.first == "s path") kv.second = wavPath;
            if (kv.first == "i seed") kv.second = "12345";
            if (kv.first == "i generation") kv.second = "4";
         }
         MolderNode respawned;
         Patch::LoadParams(&respawned, saved);
         respawned.ReloadFromPath();
         const bool keptOk = respawned.Seed() == 12345u && respawned.Generation() == 4;
         MolderNode fresh;
         Patch::LoadParams(&fresh, saved);
         const bool loadedOk = fresh.LoadFile(wavPath);
         const bool rolledOk = loadedOk && fresh.Generation() == 0;
         printf("MOLDERTEST reload keeps seed+generation (seed %u gen %d, fresh load gen %d) %s\n",
                respawned.Seed(), respawned.Generation(), fresh.Generation(),
                keptOk && rolledOk ? "OK" : "FAIL");
         ok = ok && keptOk && rolledOk;

         // A patch or remote set can carry any int. The reload's render dispatch
         // replays one Mutate per generation on the main thread, so 2^31 used to
         // freeze the UI for minutes; it is capped at kMaxGeneration.
         for (auto& kv : saved)
            if (kv.first == "i generation") kv.second = "2147483647";
         MolderNode huge;
         Patch::LoadParams(&huge, saved);
         const auto t0 = std::chrono::steady_clock::now();
         huge.ReloadFromPath();
         const double reloadSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
         const int loadedGen = huge.Generation();
         huge.Roll();
         const bool hugeOk = loadedGen == MolderNode::kMaxGeneration &&
                             huge.Generation() == MolderNode::kMaxGeneration && reloadSec < 2.0;
         printf("MOLDERTEST generation 2147483647 is capped (gen %d, after roll %d, reload %.3f s) %s\n",
                loadedGen, huge.Generation(), reloadSec, hugeOk ? "OK" : "FAIL");
         ok = ok && hugeOk;
      }
   }

   printf("%s\n", ok ? "MOLDERTEST OK" : "MOLDERTEST FAIL");
   return ok;
}

// ================================================== INFINITE_SPOUTLOOPTEST
//
// In-process loopback of Platform::Syphon* (backed by Spout2 on Windows,
// see src/platform/win/PlatformWinSyphon.cpp): create a server and a client
// in the same process, publish a small synthetic texture, receive it back,
// and check the read-back pixels match - the one thing verifiable without a
// second real app or real Windows hardware (see local-prompts/11-spout-windows.md
// section 6). Needs a live GL context, so this runs after glfwCreateWindow +
// gladLoadGL in main(), not in the pre-GLFW dispatch block above.
//
// Skips (does not FAIL) on macOS - Syphon itself needs no such test, it's
// real inter-app tech exercised by using it, not by a loopback fixture - and
// skips again on a Windows machine without WGL_NV_DX_interop2, per the
// spec's "fail soft, don't crash" requirement: this fixture can't tell "no
// interop hardware" apart from "something is broken" without real hardware
// to compare against, so it treats "never got a frame" as SKIP, not FAIL.
bool RunSpoutLoopTest()
{
#if !defined(_WIN32)
   printf("SPOUTLOOPTEST SKIP (Spout is Windows-only)\n");
   return true;
#else
   using namespace Platform;

   const std::string senderName = "InfiniteSpoutLoopTest";
   SyphonServerHandle* server = SyphonServerCreate(senderName);
   if (server == nullptr)
   {
      printf("SPOUTLOOPTEST FAIL (SyphonServerCreate returned null)\n");
      return false;
   }

   // Solid red in the top-left quadrant, solid blue everywhere else: a
   // diagonal split, unlike a plain top/bottom split, is disturbed by either
   // a vertical *or* horizontal flip bug, not just a wrong-color one.
   const int w = 64, h = 64;
   std::vector<unsigned char> srcPixels(w * h * 4);
   for (int y = 0; y < h; y++)
   {
      for (int x = 0; x < w; x++)
      {
         unsigned char* p = &srcPixels[(size_t)(y * w + x) * 4];
         bool topLeft = x < w / 2 && y < h / 2;
         p[0] = topLeft ? 255 : 0;
         p[1] = 0;
         p[2] = topLeft ? 0 : 255;
         p[3] = 255;
      }
   }

   unsigned int srcTex = 0;
   glGenTextures(1, &srcTex);
   glBindTexture(GL_TEXTURE_2D, srcTex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, srcPixels.data());
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glBindTexture(GL_TEXTURE_2D, 0);

   // Contract check, before any receiver exists: a backend that cannot see
   // receivers must never claim one. Spout used to return IsInitialized()
   // here, so after the first send the node read "Clients: Active" alone.
   SyphonServerPublish(server, srcTex, w, h, false);
   bool ok = true;
   if (!SyphonServerCanReportClients() && SyphonServerHasClients(server))
   {
      printf("SPOUTLOOPTEST FAIL (HasClients true with no receiver, on a backend that cannot report receivers)\n");
      ok = false;
   }

   SyphonClientHandle* client = SyphonClientCreate();
   bool connected = client != nullptr && SyphonClientConnect(client, "Spout", senderName);

   bool skipped = false;
   if (!connected)
   {
      printf("SPOUTLOOPTEST SKIP (no Spout sender visible - likely no WGL_NV_DX_interop2 on this machine)\n");
      skipped = true;
   }
   else
   {
      unsigned int recvTex = 0;
      int recvW = 0, recvH = 0;
      // One-frame lag by design (see PlatformWinSyphon.cpp): the first
      // GetFrameTexture call after a size change only reallocates and
      // returns 0, the copy happens on the next call.
      for (int attempt = 0; attempt < 10 && recvTex == 0; attempt++)
      {
         SyphonServerPublish(server, srcTex, w, h, false);
         recvTex = SyphonClientGetFrameTexture(client, recvW, recvH);
      }

      if (recvTex == 0)
      {
         printf("SPOUTLOOPTEST SKIP (never received a frame - likely no WGL_NV_DX_interop2 on this machine)\n");
         skipped = true;
      }
      else if (recvW != w || recvH != h)
      {
         printf("SPOUTLOOPTEST FAIL (size mismatch: got %dx%d, expected %dx%d)\n", recvW, recvH, w, h);
         ok = false;
      }
      else
      {
         // recvTex is GL_TEXTURE_RECTANGLE, per SyphonClientGetFrameTexture's
         // documented contract, so read it back through an FBO.
         unsigned int fbo = 0;
         glGenFramebuffers(1, &fbo);
         glBindFramebuffer(GL_FRAMEBUFFER, fbo);
         glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, recvTex, 0);
         std::vector<unsigned char> readBack((size_t)w * h * 4);
         glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, readBack.data());
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glDeleteFramebuffers(1, &fbo);

         auto pixelAt = [&](int x, int y) { return &readBack[(size_t)(y * w + x) * 4]; };
         unsigned char* tl = pixelAt(w / 4, h / 4);
         unsigned char* br = pixelAt(3 * w / 4, 3 * h / 4);
         bool topLeftRed = tl[0] > 200 && tl[2] < 50;
         bool bottomRightBlue = br[2] > 200 && br[0] < 50;
         if (!topLeftRed || !bottomRightBlue)
         {
            printf("SPOUTLOOPTEST FAIL (pixel/orientation mismatch: topLeft=(%d,%d,%d) bottomRight=(%d,%d,%d))\n",
                   tl[0], tl[1], tl[2], br[0], br[1], br[2]);
            ok = false;
         }
      }
   }

   SyphonClientDestroy(client);
   glDeleteTextures(1, &srcTex);
   SyphonServerDestroy(server);

   if (!skipped)
      printf("%s\n", ok ? "SPOUTLOOPTEST OK" : "SPOUTLOOPTEST FAIL");
   return ok;
#endif
}

// Granular Synthesizer's DSP fixture: loads a real WAV, triggers granular playback,
// and asserts that position, scan, grain length, density, spray, pitch shift,
// window shapes, and volume produce finite, valid audio outputs.
bool RunGranularFixture()
{
   const int numFrames = 44100; // 1.0s @ 44100 Hz
   const int sampleRate = 44100;
   std::vector<int16_t> pcm(numFrames);
   for (int i = 0; i < numFrames; i++)
   {
      const float t = (float)i / (float)(numFrames - 1);
      pcm[i] = (int16_t)(std::sin(t * 440.0f * 6.283185f) * 30000.0f);
   }

   const std::string path = TmpPath("infinite_granular_fixture.wav");
   {
      std::ofstream f(path, std::ios::binary);
      auto writeU32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
      auto writeU16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
      const uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
      f.write("RIFF", 4); writeU32(36 + dataSize); f.write("WAVE", 4);
      f.write("fmt ", 4); writeU32(16); writeU16(1); writeU16(1);
      writeU32(sampleRate); writeU32(sampleRate * 2); writeU16(2); writeU16(16);
      f.write("data", 4); writeU32(dataSize);
      f.write((const char*)pcm.data(), dataSize);
   }

   bool ok = true;

   auto trigger = [&](float pos, float scan, float len, float dens, float spray, float pitch, int winShape, int frames) -> std::vector<float>
   {
      GranularNode node;
      if (!node.LoadFile(path))
      {
         ok = false;
         return {};
      }
      node.position = pos;
      node.scan = scan;
      node.grainLength = len;
      node.density = dens;
      node.randomPos = spray;
      node.pitchShift = pitch;
      node.windowShape = winShape;
      node.volume = 0.8f;
      node.CookIfNeeded(1);

      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay((double)sampleRate, 2048);
      node.CookIfNeeded(2);

      std::vector<float> l(frames, 0.0f), r(frames, 0.0f);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = frames;
      for (int block = 0; block < 4; ++block)
         an->ProcessBlock(nullptr, 0, buf);
      return l;
   };

   const auto baseline = trigger(0.2f, 1.0f, 50.0f, 30.0f, 0.05f, 0.0f, 0, 1024);
   const auto pitched = trigger(0.2f, 1.0f, 50.0f, 30.0f, 0.05f, 7.0f, 0, 1024);
   const auto gaussianWin = trigger(0.2f, 0.0f, 100.0f, 50.0f, 0.2f, 0.0f, 1, 1024);

   if (baseline.empty() || pitched.empty() || gaussianWin.empty())
   {
      printf("GRANULARTEST BUG (fixture failed to load test file)\n");
      ok = false;
   }
   else
   {
      for (float s : baseline)
      {
         if (!std::isfinite(s)) { printf("GRANULARTEST baseline produced non-finite sample FAIL\n"); ok = false; break; }
      }
      for (float s : pitched)
      {
         if (!std::isfinite(s)) { printf("GRANULARTEST pitched produced non-finite sample FAIL\n"); ok = false; break; }
      }
      for (float s : gaussianWin)
      {
         if (!std::isfinite(s)) { printf("GRANULARTEST gaussianWin produced non-finite sample FAIL\n"); ok = false; break; }
      }

      // Record test: verify StartRecording arms RequiresAudioProcessing and captures audio on input slot 0
      {
         GranularNode recNode;
         recNode.StartRecording();
         if (!recNode.IsRecording() || !recNode.RequiresAudioProcessing())
         {
            printf("GRANULARTEST record arm failed FAIL\n");
            ok = false;
         }
         else
         {
            AudioNode* an = recNode.GetAudioNode();
            an->PrepareToPlay((double)sampleRate, 128);
            std::vector<float> tone(128);
            for (int i = 0; i < 128; i++)
               tone[i] = sinf((float)i * 0.3f);
            float* srcChans[1] = { tone.data() };
            AudioBuffer srcBuf;
            srcBuf.channels = srcChans;
            srcBuf.numChannels = 1;
            srcBuf.numFrames = 128;

            std::vector<float> outL(128, 0.0f), outR(128, 0.0f);
            float* outChans[2] = { outL.data(), outR.data() };
            AudioBuffer outBuf;
            outBuf.channels = outChans;
            outBuf.numChannels = 2;
            outBuf.numFrames = 128;

            const AudioBuffer* inputs[1] = { &srcBuf };
            an->ProcessBlock(inputs, 1, outBuf);

            recNode.StopRecording();
            if (recNode.RequiresAudioProcessing())
            {
               printf("GRANULARTEST RequiresAudioProcessing should be false after stop FAIL\n");
               ok = false;
            }
            if (recNode.waveformCacheCount <= 0 || recNode.FileName() != "recording" || recNode.FilePath().empty())
            {
               printf("GRANULARTEST recording buffer capture failed FAIL\n");
               ok = false;
            }
            else
            {
               GranularNode reloaded;
               std::vector<std::pair<std::string, std::string>> params;
               Patch::SaveParams(&recNode, params);
               Patch::LoadParams(&reloaded, params);
               reloaded.ReloadFromPath();
               if (reloaded.waveformCacheCount <= 0 || reloaded.FilePath().empty())
               {
                  printf("GRANULARTEST record patch reload failed FAIL\n");
                  ok = false;
               }
            }
         }
      }

      // Trim bounds test: verify playhead, seek, and grain positions stay strictly within [start, end]
      {
         GranularNode trimNode;
         if (trimNode.LoadFile(path))
         {
            trimNode.start = 0.3f;
            trimNode.end = 0.6f;
            trimNode.position = 0.1f; // Out of bounds
            trimNode.CookIfNeeded(10);
            if (trimNode.position < 0.299f || trimNode.position > 0.601f)
            {
               printf("GRANULARTEST initial position not clamped to [start, end] FAIL\n");
               ok = false;
            }

            trimNode.Seek(0.8f); // Out of bounds seek
            if (trimNode.position < 0.299f || trimNode.position > 0.601f || trimNode.Playhead() > 0.601f)
            {
               printf("GRANULARTEST seek beyond end not clamped FAIL\n");
               ok = false;
            }

            AudioNode* an = trimNode.GetAudioNode();
            an->PrepareToPlay((double)sampleRate, 512);
            trimNode.scan = 1.5f;
            trimNode.randomPos = 0.8f; // Wide spray
            trimNode.CookIfNeeded(11);

            std::vector<float> l(512, 0.0f), r(512, 0.0f);
            float* chans[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = chans;
            buf.numChannels = 2;
            buf.numFrames = 512;

            for (int b = 0; b < 20; ++b)
            {
               an->ProcessBlock(nullptr, 0, buf);
               trimNode.CookIfNeeded(12 + b);
               const float ph = trimNode.Playhead();
               if (ph < 0.299f || ph > 0.601f)
               {
                  printf("GRANULARTEST playhead out of bounds: %.4f not in [0.3, 0.6] FAIL\n", ph);
                  ok = false;
                  break;
               }

               const auto& snap = trimNode.VisualSnapshot();
               for (int g = 0; g < snap.count; ++g)
               {
                  if (snap.grains[g].position < 0.299f || snap.grains[g].position > 0.601f)
                  {
                     printf("GRANULARTEST grain position out of bounds: %.4f not in [0.3, 0.6] FAIL\n", snap.grains[g].position);
                     ok = false;
                     break;
                  }
               }
               if (!ok) break;
            }
         }
      }
   }

   remove(path.c_str());
   printf("%s\n", ok ? "GRANULARTEST OK" : "GRANULARTEST FAIL");
   return ok;
}

// Convolution Reverb (v0.6 2.9): the convolver against a direct convolution,
// the swap between two impulse responses, the designed room against what its
// controls promise, and the cost of a 5 s response.
bool RunConvolutionFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   auto verdict = [&](const char* what, bool pass, const char* fmt, double a, double b = 0.0) {
      char num[160];
      snprintf(num, sizeof(num), fmt, a, b);
      printf("CONVOLUTIONTEST %s: %s %s\n", what, num, pass ? "OK" : "FAIL");
      ok = ok && pass;
   };
   uint32_t seed = 0x1234567u;
   auto noise = [&]() {
      seed ^= seed << 13;
      seed ^= seed >> 17;
      seed ^= seed << 5;
      return (float)(int32_t)seed * (1.0f / 2147483648.0f);
   };
   // Runs x (stereo) through the kernel in uneven blocks; `swapAt` >= 0 installs `swapTo` at that sample.
   auto run = [&](ConvolutionKernel& k, const std::vector<float>& xL, const std::vector<float>& xR, std::vector<float>& yL,
                  std::vector<float>& yR, int swapAt = -1, ConvolutionKernel::IrSet* swapTo = nullptr) {
      static const int kSizes[] = { 64, 512, 37, 128, 1, 300, 256 };
      const int n = (int)xL.size();
      yL.assign(n, 0.0f);
      yR.assign(n, 0.0f);
      for (int pos = 0, b = 0; pos < n; b++)
      {
         const int frames = std::min(kSizes[b % 7], n - pos);
         if (swapTo && pos >= swapAt)
         {
            k.InstallForTest(swapTo);
            swapTo = nullptr;
         }
         float* iCh[2] = { const_cast<float*>(xL.data()) + pos, const_cast<float*>(xR.data()) + pos };
         float* oCh[2] = { yL.data() + pos, yR.data() + pos };
         AudioBuffer inBuf { iCh, 2, frames };
         AudioBuffer outBuf { oCh, 2, frames };
         k.ProcessBlock(inBuf, nullptr, outBuf);
         pos += frames;
      }
   };
   auto direct = [](const std::vector<float>& x, const std::vector<float>& h, std::vector<double>& y) {
      const int n = (int)x.size(), len = (int)h.size();
      y.assign(n, 0.0);
      for (int i = 0; i < n; i++)
      {
         double acc = 0.0;
         const int kMax = std::min(len - 1, i);
         for (int k = 0; k <= kMax; k++)
            acc += (double)h[k] * (double)x[i - k];
         y[i] = acc;
      }
   };
   auto errorDb = [](const std::vector<float>& y, const std::vector<double>& ref) {
      double err = 0.0, sig = 0.0;
      for (size_t i = 0; i < y.size(); i++)
      {
         err += ((double)y[i] - ref[i]) * ((double)y[i] - ref[i]);
         sig += ref[i] * ref[i];
      }
      return 10.0 * log10((err + 1e-30) / (sig + 1e-30));
   };

   // --- 1. null against a direct convolution; every tier in play, no delay ---
   const int irLen = 22000, sigLen = 30000;
   std::vector<float> hL(irLen), hR(irLen), xL(sigLen), xR(sigLen);
   for (int i = 0; i < irLen; i++)
   {
      const float env = expf(-(float)i / 6000.0f);
      hL[i] = noise() * env;
      hR[i] = noise() * env;
   }
   hL[0] = 1.0f; // the very first tap must come out on the very first sample
   for (int i = 0; i < sigLen; i++)
   {
      xL[i] = noise();
      xR[i] = noise();
   }
   std::vector<double> refL, refR;
   direct(xL, hL, refL);
   direct(xR, hR, refR);
   std::vector<float> yL, yR;
   {
      ConvolutionKernel k;
      k.PrepareToPlay(sr, 512);
      std::vector<float> a = hL, b = hR;
      k.InstallForTest(ConvolutionKernel::BuildSet(a, b, sr, false));
      run(k, xL, xR, yL, yR);
      const double eL = errorDb(yL, refL), eR = errorDb(yR, refR);
      verdict("null vs direct convolution", eL <= -90.0 && eR <= -90.0, "L %.1f dB, R %.1f dB (limit -90)", eL, eR);
      verdict("latency", k.LatencySamples() == 0 && fabs((double)yL[0] - (double)xL[0]) < 1e-5, "%.0f samples, first sample error %.2g",
              (double)k.LatencySamples(), fabs((double)yL[0] - (double)xL[0]));
   }

   // --- 2. swapping to an identical response changes nothing; to a different one, no click ---
   {
      ConvolutionKernel k;
      k.PrepareToPlay(sr, 512);
      std::vector<float> a = hL, b = hR, c = hL, d = hR;
      k.InstallForTest(ConvolutionKernel::BuildSet(a, b, sr, false));
      std::vector<float> zL, zR;
      run(k, xL, xR, zL, zR, 9000, ConvolutionKernel::BuildSet(c, d, sr, false));
      const double e = errorDb(zL, refL);
      verdict("swap to the same response", e <= -90.0, "%.1f dB (limit -90)", e);
   }
   {
      const int n = 48000;
      std::vector<float> sL(n), sR(n), zL, zR;
      for (int i = 0; i < n; i++)
         sL[i] = sR[i] = sinf(2.0f * (float)M_PI * 220.0f * (float)i / (float)sr);
      ConvolutionKernel::Design small, large;
      small.size = 0.2f;
      small.decay = 0.4f;
      large.size = 0.8f;
      large.decay = 0.6f;
      large.shape = 1;
      std::vector<float> a, b, c, d;
      ConvolutionKernel::DesignImpulse(small, sr, a, b);
      ConvolutionKernel::DesignImpulse(large, sr, c, d);
      ConvolutionKernel k;
      k.PrepareToPlay(sr, 512);
      k.InstallForTest(ConvolutionKernel::BuildSet(a, b, sr));
      run(k, sL, sR, zL, zR, 30000, ConvolutionKernel::BuildSet(c, d, sr));
      // A 220 Hz sine through any linear system steps by at most peak * 2 pi f / sr per sample.
      double peak = 0.0, step = 0.0;
      for (int i = 24000; i < n; i++)
      {
         peak = std::max(peak, (double)fabsf(zL[i]));
         step = std::max(step, (double)fabsf(zL[i] - zL[i - 1]));
      }
      const double smooth = peak * 2.0 * M_PI * 220.0 / sr;
      verdict("swap to a different response", peak > 0.01 && step < 3.0 * smooth, "largest step %.5f, a click-free sine allows %.5f",
              step, 3.0 * smooth);
   }

   // --- 3. the designed room ---
   // Decay time of one band, from the backward-integrated energy between -5 and -25 dB.
   auto decayTime = [&](const std::vector<float>& h, int band) {
      std::vector<float> f(h.size());
      DspMath::Biquad q1, q2;
      if (band == 0)
      {
         q1.SetBandpass(700.0, 1.5, sr);
         q2.SetBandpass(700.0, 1.5, sr);
      }
      else
      {
         q1.SetHighpass(7000.0, 0.7071, sr);
         q2.SetHighpass(7000.0, 0.7071, sr);
      }
      for (size_t i = 0; i < h.size(); i++)
         f[i] = q2.Process(q1.Process(h[i]));
      std::vector<double> edc(f.size());
      double acc = 0.0;
      for (int i = (int)f.size() - 1; i >= 0; i--)
      {
         acc += (double)f[i] * (double)f[i];
         edc[i] = acc;
      }
      int t5 = -1, t25 = -1;
      for (int i = 0; i < (int)edc.size(); i++)
      {
         const double db = 10.0 * log10(edc[i] / (edc[0] + 1e-30) + 1e-30);
         if (t5 < 0 && db <= -5.0)
            t5 = i;
         if (t25 < 0 && db <= -25.0)
            t25 = i;
      }
      return (t5 >= 0 && t25 > t5) ? 3.0 * (double)(t25 - t5) / sr : 0.0;
   };
   for (float want : { 0.8f, 2.5f })
   {
      ConvolutionKernel::Design d;
      d.decay = want;
      std::vector<float> l, r;
      ConvolutionKernel::DesignImpulse(d, sr, l, r);
      const double got = decayTime(l, 0);
      char what[48];
      snprintf(what, sizeof(what), "decay %.1f s", want);
      verdict(what, fabs(got / want - 1.0) < 0.2, "measured %.2f s at 700 Hz (within 20%%)", got);
   }
   {
      ConvolutionKernel::Design soft, hard;
      soft.material = 0.0f;
      hard.material = 1.0f;
      std::vector<float> sl, sr2, hl, hr;
      ConvolutionKernel::DesignImpulse(soft, sr, sl, sr2);
      ConvolutionKernel::DesignImpulse(hard, sr, hl, hr);
      const double ts = decayTime(sl, 1), th = decayTime(hl, 1);
      verdict("material", ts > 0.0 && ts < 0.7 * th, "highs ring %.2f s soft, %.2f s hard", ts, th);
   }
   {
      ConvolutionKernel::Design mono, wide;
      mono.spread = 0.0f;
      wide.spread = 1.0f;
      std::vector<float> ml, mr, wl, wr;
      ConvolutionKernel::DesignImpulse(mono, sr, ml, mr);
      ConvolutionKernel::DesignImpulse(wide, sr, wl, wr);
      double diff = 0.0, lr = 0.0, ll = 0.0, rr = 0.0;
      for (size_t i = 0; i < ml.size(); i++)
         diff = std::max(diff, (double)fabsf(ml[i] - mr[i]));
      for (size_t i = 4800; i < wl.size(); i++)
      {
         lr += (double)wl[i] * wr[i];
         ll += (double)wl[i] * wl[i];
         rr += (double)wr[i] * wr[i];
      }
      const double corr = lr / sqrt(ll * rr + 1e-30);
      verdict("spread", diff == 0.0 && fabs(corr) < 0.2, "spread 0: L-R difference %.2g; spread 1: tail correlation %.2f", diff, corr);
   }
   {
      // Direct sound against everything after it.
      auto directShare = [&](float distance, float size) {
         ConvolutionKernel::Design d;
         d.distance = distance;
         d.size = size;
         std::vector<float> l, r;
         ConvolutionKernel::DesignImpulse(d, sr, l, r);
         double total = 0.0;
         for (float v : l)
            total += (double)v * v;
         return ((double)l[0] * l[0] + (double)l[1] * l[1]) / total;
      };
      const double nearShare = directShare(0.1f, 0.45f), farShare = directShare(0.9f, 0.45f);
      verdict("distance", nearShare > 4.0 * farShare, "direct sound is %.3f of the energy near, %.3f far", nearShare, farShare);
      // First reflection arrives later in a larger room.
      auto firstReflection = [&](float size, int shape = 0) {
         ConvolutionKernel::Design d;
         d.size = size;
         d.shape = shape;
         std::vector<float> l, r;
         ConvolutionKernel::DesignImpulse(d, sr, l, r);
         for (int i = 8; i < (int)l.size(); i++)
            if (fabsf(l[i]) > 0.05f * fabsf(l[0]))
               return (double)i / sr * 1000.0;
         return 0.0;
      };
      const double smallMs = firstReflection(0.1f), largeMs = firstReflection(0.9f);
      verdict("size", largeMs > 3.0 * smallMs && smallMs > 0.0, "first reflection after %.1f ms small, %.1f ms large", smallMs, largeMs);
      // A tunnel's side walls are much closer than a room's of the same length.
      const double roomMs = firstReflection(0.6f, 0), tunnelMs = firstReflection(0.6f, 3);
      verdict("shape", tunnelMs > 0.0 && tunnelMs < 0.6 * roomMs, "first reflection after %.1f ms in a room, %.1f ms in a tunnel", roomMs,
              tunnelMs);
   }

   // --- 4. a file at another rate keeps its pitch and length ---
   {
      const int frames = 44100;
      std::vector<float> f(frames), l, r;
      for (int i = 0; i < frames; i++)
         f[i] = sinf(2.0f * (float)M_PI * 1000.0f * (float)i / 44100.0f);
      ConvolutionKernel::ImpulseFromFile(f.data(), nullptr, frames, 44100.0, sr, l, r);
      double dot = 0.0, a = 0.0, b = 0.0;
      for (int i = 2000; i + 2000 < (int)l.size(); i++)
      {
         const double want = sin(2.0 * M_PI * 1000.0 * (double)i / sr);
         dot += want * l[i];
         a += want * want;
         b += (double)l[i] * l[i];
      }
      const double corr = dot / sqrt(a * b + 1e-30);
      verdict("file resampled 44.1 to 48 kHz", abs((int)l.size() - 48000) <= 1 && corr > 0.9999 && fabs(b / a - 1.0) < 0.01,
              "%.0f samples, correlation %.6f", (double)l.size(), corr);
   }

   // --- 5. through the node: built in the background, rebuilt on a change, a bad file falls back ---
   {
      const EffectDef* def = nullptr;
      for (const auto& d : GetEffectDefs())
         if (d.name == "Convolution Reverb")
            def = &d;
      if (!def)
      {
         printf("CONVOLUTIONTEST def not found FAIL\n");
         return false;
      }
      AudioEffectNode node(*def);
      ConvolutionKernel k;
      k.PrepareToPlay(sr, 512);
      auto settle = [&]() {
         for (int tries = 0; tries < 1000; tries++)
         {
            k.PushParams(node, sr);
            if (!k.Preparing())
               break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
         }
         k.PushParams(node, sr);
      };
      settle();
      float envA[ConvolutionKernel::kEnvelopePoints], envB[ConvolutionKernel::kEnvelopePoints];
      float secA = 0.0f, secB = 0.0f;
      const int nA = k.Envelope(envA, ConvolutionKernel::kEnvelopePoints, &secA);
      std::vector<float> iL(4800, 0.0f), zL, zR;
      iL[0] = 1.0f;
      run(k, iL, iL, zL, zR);
      double energy = 0.0;
      for (float v : zL)
         energy += (double)v * v;
      verdict("node builds its space", nA == ConvolutionKernel::kEnvelopePoints && !k.Preparing() && energy > 0.01,
              "%.2f s response, impulse energy out %.3f", secA, energy);
      *node.ParamPtr("decay") = 4.0f;
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      settle();
      k.Envelope(envB, ConvolutionKernel::kEnvelopePoints, &secB);
      verdict("node rebuilds on a change", secB > 2.0f * secA, "%.2f s -> %.2f s", secA, secB);
      node.filePath = "/nonexistent/impulse.wav";
      std::this_thread::sleep_for(std::chrono::milliseconds(80));
      settle();
      verdict("missing file", !k.FileError().empty() && !k.UsingFile() && !k.Preparing(), "falls back to the designed space, reason given in %.0f characters",
              (double)k.FileError().size());
   }

   // --- 6. cost: a 5 s response at 48 kHz, stereo, 128-frame blocks ---
   {
      ConvolutionKernel::Design d;
      d.decay = (5.0f - 0.05f) / 1.15f;
      std::vector<float> l, r;
      ConvolutionKernel::DesignImpulse(d, sr, l, r);
      const double irSeconds = (double)l.size() / sr;
      ConvolutionKernel k;
      k.PrepareToPlay(sr, 128);
      k.InstallForTest(ConvolutionKernel::BuildSet(l, r, sr));
      const int frames = 128, blocks = (int)(10.0 * sr / frames);
      std::vector<float> iL(frames), iR(frames), oL(frames), oR(frames);
      float* iCh[2] = { iL.data(), iR.data() };
      float* oCh[2] = { oL.data(), oR.data() };
      AudioBuffer inBuf { iCh, 2, frames };
      AudioBuffer outBuf { oCh, 2, frames };
      const double blockSeconds = (double)frames / sr;
      // Wall-clock timing on an ordinary thread picks up whatever else the machine is doing, so the gate takes the
      // best of three passes: a real cost shows in all of them, a busy Mac in only some.
      struct Pass { double average, p50, p99, p999, peak; int worstBlock, over; };
      auto runPass = [&]() {
         double total = 0.0, worst = 0.0;
         int worstBlock = 0, over = 0;
         std::vector<double> blockTimes((size_t)blocks);
         for (int b = 0; b < blocks; b++)
         {
            for (int i = 0; i < frames; i++)
            {
               iL[i] = noise();
               iR[i] = noise();
            }
            const auto t0 = std::chrono::steady_clock::now();
            k.ProcessBlock(inBuf, nullptr, outBuf);
            const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            total += dt;
            blockTimes[(size_t)b] = dt;
            over += dt > blockSeconds ? 1 : 0;
            if (dt > worst)
            {
               worst = dt;
               worstBlock = b;
            }
         }
         std::vector<double> sorted = blockTimes;
         std::sort(sorted.begin(), sorted.end());
         auto pct = [&](double q) { return 100.0 * sorted[(size_t)std::min((double)(blocks - 1), q * (double)blocks)] / blockSeconds; };
         return Pass { 100.0 * total / ((double)blocks * blockSeconds), pct(0.5), pct(0.99), pct(0.999),
                       100.0 * worst / blockSeconds, worstBlock, over };
      };
      Pass best = runPass();
      for (int p = 1; p < 3; p++)
      {
         const Pass next = runPass();
         best.average = std::min(best.average, next.average);
         if (next.p99 < best.p99)
         {
            best.p50 = next.p50;
            best.p99 = next.p99;
            best.p999 = next.p999;
            best.peak = next.peak;
            best.worstBlock = next.worstBlock;
            best.over = next.over;
         }
      }
      printf("CONVOLUTIONTEST cost: %.2f s response, worst single block %.1f%% of its time slot (best of 3)\n", irSeconds, best.peak);
      printf("CONVOLUTIONTEST block times: p50 %.1f%%, p99 %.1f%%, p99.9 %.1f%% of the slot; worst at block %d of %d; %d blocks over 100%%\n",
             best.p50, best.p99, best.p999, best.worstBlock, blocks, best.over);
      verdict("cost", irSeconds > 4.9 && best.average < 3.0 && best.p99 < 50.0,
              "%.2f%% of one core on average (limit 3%%), p99 block %.1f%% of its slot (limit 50%%), best of 3", best.average, best.p99);
   }

   printf("CONVOLUTIONTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

// MSEG (v0.6 2.10): the shape maths, the save string, and the run over time
// with and without a trig cable. A straight 0 -> 1 ramp is the probe shape,
// so the value read back is the position along the shape.
bool RunMSEGFixture()
{
   bool all = true;
   auto check = [&](const char* what, double got, double want, double tol) {
      const bool ok = std::fabs(got - want) <= tol;
      printf("MSEGTEST %-58s got %.4f want %.4f  %s\n", what, got, want, ok ? "OK" : "FAIL");
      all &= ok;
   };
   struct Stub : IModulator
   {
      float level = 0.0f;
      float Value01() override { return level; }
   };

   Transport& tr = Transport::Instance();
   const float tempoBefore = (float)tr.Tempo();
   tr.SetPlaying(false);
   tr.Rewind();
   tr.SetTempo(120.0f); // 2 beats a second: a 4-beat shape takes 2 s
   tr.SetPlaying(true);
   // The node under test is read every tick, as the app reads it every frame.
   MSEGNode* live = nullptr;
   auto advance = [&](double seconds) {
      while (seconds > 1.0e-9)
      {
         const float step = (float)std::min(seconds, 0.005);
         tr.Tick(step);
         seconds -= step;
         if (live != nullptr)
            live->Value01();
      }
   };
   auto ramp = [](MSEGNode& n) { n.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }; };

   // --- shape maths
   check("segment straight, halfway", MSEGNode::Segment(0.0f, 1.0f, 0.0f, 0.5f), 0.5, 1e-6);
   check("segment ends are exact (bend 1)", MSEGNode::Segment(0.2f, 0.9f, 1.0f, 1.0f), 0.9, 1e-6);
   check("segment bend +1 halfway = 1/(1+e^-3)", MSEGNode::Segment(0.0f, 1.0f, 1.0f, 0.5f), 1.0 / (1.0 + std::exp(-3.0)), 1e-5);
   check("segment bend -1 halfway = 1/(1+e^3)", MSEGNode::Segment(0.0f, 1.0f, -1.0f, 0.5f), 1.0 / (1.0 + std::exp(3.0)), 1e-5);
   {
      MSEGNode n;
      n.points = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
      check("triangle at 0.25", n.Evaluate(0.25f), 0.5, 1e-6);
      check("triangle at 0.5", n.Evaluate(0.5f), 1.0, 1e-6);
      check("triangle at 0.75", n.Evaluate(0.75f), 0.5, 1e-6);
      const int at = n.AddPoint(0.25f, 0.9f);
      check("added point lands in order (index)", at, 1, 0);
      n.MovePoint(1, 0.8f, 0.9f); // tries to pass the point at 0.5
      check("a moved point stops at its neighbour", n.points[1].x, 0.5, 1e-6);
      n.RemovePoint(0);
      n.RemovePoint((int)n.points.size() - 1);
      check("end points cannot be removed (count)", (double)n.points.size(), 4, 0);
      n.points[1].curve = -0.35f;
      MSEGNode back;
      back.Decode(n.Encode());
      bool same = back.points.size() == n.points.size();
      for (size_t i = 0; same && i < n.points.size(); i++)
         same = std::fabs(back.points[i].x - n.points[i].x) < 1e-5f && std::fabs(back.points[i].y - n.points[i].y) < 1e-5f &&
                std::fabs(back.points[i].curve - n.points[i].curve) < 1e-5f;
      check("save string round trip (points and bends)", same ? 1 : 0, 1, 0);
      back.Decode("garbage");
      check("a bad save string leaves the shape alone (count)", (double)back.points.size(), 4, 0);
   }

   // --- nothing patched: follows the transport
   {
      MSEGNode n;
      ramp(n);
      n.rateMode = LFONode::kRateSynced;
      n.rateBeats = 4.0f;
      advance(1.0);
      check("unpatched, 4 beats at 120: 1 s in", n.Value01(), 0.5, 0.01);
      float first = n.Value01();
      bool steady = true;
      for (int i = 0; i < 8; i++)
         steady &= n.Value01() == first;
      check("eight reads in one tick agree", steady ? 1 : 0, 1, 0);
      advance(1.5);
      check("unpatched, loop off repeats: 2.5 s in", n.Value01(), 0.25, 0.01);
      n.loop = true;
      n.loopStart = 0.25f;
      n.loopEnd = 0.75f;
      // 2.5 s = 1.25 lengths: 0.25 + fmod(1.25 - 0.25, 0.5) = 0.25
      advance(0.5); // 3.0 s = 1.5 lengths -> 0.25 + fmod(1.25, 0.5) = 0.5
      check("unpatched, loop 0.25-0.75: 3 s in", n.Value01(), 0.5, 0.01);
      tr.SetPlaying(false);
      const float held = n.Value01();
      advance(0.5);
      check("paused transport holds it", n.Value01(), held, 1e-6);
      tr.SetPlaying(true);
   }

   // --- trig patched, loop off: one run, then the last value
   {
      MSEGNode n, twin;
      Stub trig;
      ramp(n);
      ramp(twin);
      n.trigger = twin.trigger = &trig;
      live = &n;
      n.rateBeats = twin.rateBeats = 4.0f;
      check("at rest before any trig: last value", n.Value01(), 1.0, 1e-6);
      twin.Value01();
      trig.level = 1.0f;
      double worst = 0.0;
      for (int i = 0; i < 200; i++) // 1 s
      {
         tr.Tick(0.005f);
         const float one = twin.Value01();
         float many = 0.0f;
         for (int k = 0; k < 8; k++)
            many = n.Value01();
         worst = std::max(worst, (double)std::fabs(one - many));
      }
      check("run read 8 times a tick = read once (max diff)", worst, 0.0, 1e-7);
      check("trig, 1 s in", n.Value01(), 0.5, 0.01);
      trig.level = 0.0f; // loop off: letting go changes nothing
      advance(0.5);
      check("loop off ignores trig dropping: 1.5 s in", n.Value01(), 0.75, 0.01);
      advance(1.0);
      check("loop off holds the last value after the run", n.Value01(), 1.0, 1e-6);
      trig.level = 1.0f;
      advance(0.25);
      check("retrigger starts over: 0.25 s after the edge", n.Value01(), 0.125, 0.01);
      advance(0.25);
      trig.level = 0.0f;
      advance(0.01);
      trig.level = 1.0f;
      advance(0.1);
      check("retrigger in the middle of a run", n.Value01(), 0.05, 0.01);
      live = nullptr;
   }

   // --- trig patched, loop on: loop while held, release to the end
   {
      MSEGNode n;
      Stub trig;
      ramp(n);
      n.trigger = &trig;
      live = &n;
      n.rateBeats = 4.0f;
      n.loop = true;
      n.loopStart = 0.25f;
      n.loopEnd = 0.75f;
      n.Value01();
      trig.level = 1.0f;
      advance(3.0); // 1.5 lengths -> 0.5 inside the loop
      check("held, loop 0.25-0.75: 3 s in", n.Value01(), 0.5, 0.012);
      float lo = 1.0f, hi = 0.0f;
      for (int i = 0; i < 400; i++)
      {
         tr.Tick(0.005f);
         const float v = n.Value01();
         lo = std::min(lo, v);
         hi = std::max(hi, v);
      }
      check("held loop never leaves 0.25-0.75 (low)", lo, 0.25, 0.01);
      check("held loop never leaves 0.25-0.75 (high)", hi, 0.75, 0.01);
      const float atRelease = n.Value01();
      trig.level = 0.0f;
      advance(0.2); // 0.1 lengths on
      check("release plays on from where it was", n.Value01(), atRelease + 0.1, 0.012);
      advance(2.0);
      check("release ends on the last value", n.Value01(), 1.0, 1e-6);

      // Sustain point: loop start = loop end.
      n.loopStart = n.loopEnd = 0.4f;
      trig.level = 1.0f;
      advance(3.0);
      check("sustain point holds while trig is high", n.Value01(), 0.4, 0.005);
      trig.level = 0.0f;
      advance(0.4);
      check("sustain point, 0.4 s after release", n.Value01(), 0.6, 0.012);
      live = nullptr;
   }

   // --- rate: tempo and Hz
   {
      MSEGNode n;
      Stub trig;
      ramp(n);
      n.trigger = &trig;
      live = &n;
      n.rateBeats = 4.0f;
      n.Value01();
      tr.SetTempo(240.0f);
      trig.level = 1.0f;
      advance(0.5);
      check("tempo sync: 4 beats at 240, 0.5 s in", n.Value01(), 0.5, 0.012);
      trig.level = 0.0f;
      advance(0.01);
      n.rateMode = LFONode::kRateHz;
      n.rateHz = 2.0f;
      n.Value01();
      trig.level = 1.0f;
      advance(0.25);
      check("Hz: 2 Hz, 0.25 s in", n.Value01(), 0.5, 0.012);
      n.points = { { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
      float lo = 1.0f, hi = 0.0f;
      for (float x = 0.0f; x <= 1.0f; x += 0.01f)
      {
         lo = std::min(lo, n.Evaluate(x));
         hi = std::max(hi, n.Evaluate(x));
      }
      check("output stays in 0..1 (low)", lo, 0.0, 1e-6);
      check("output stays in 0..1 (high)", hi, 1.0, 0.02);
      live = nullptr;
   }

   live = nullptr;
   tr.SetPlaying(false);
   tr.Rewind();
   tr.SetTempo(tempoBefore);
   printf("MSEGTEST %s\n", all ? "OK" : "FAIL");
   return all;
}
// Per-voice modulation (docs/plans/modulation/per-voice.md): the audio-thread
// kernel against the node it copies, note-on restarts per voice, the route's
// shaping against CookFrame's, and a real synth taking a plan.
bool RunVoiceModFixture()
{
   bool all = true;
   auto check = [&](const char* what, double got, double want, double tol) {
      const bool ok = std::fabs(got - want) <= tol;
      printf("VOICEMODTEST %-58s got %.4f want %.4f  %s\n", what, got, want, ok ? "OK" : "FAIL");
      all &= ok;
   };
   constexpr double kSr = 48000.0;
   using Rt = VoiceMod::Runtime<8>;
   auto box = std::make_unique<VoiceMod::PlanBox>();
   auto plan = std::make_unique<VoiceMod::Plan>();
   auto rt = std::make_unique<Rt>();
   rt->Prepare(kSr);
   auto oneRoute = [&](const VoiceMod::Spec& sp) {
      *plan = VoiceMod::Plan();
      plan->numSources = 1;
      plan->sources[0] = sp;
      plan->numRoutes = 1;
      plan->routes[0] = VoiceMod::Route();
      plan->routes[0].param = 0;
      box->Publish(*plan);
   };
   float last = 0.0f;
   auto run = [&](int voice, int samples) {
      for (int i = 0; i < samples; i++)
         rt->Apply(voice, [&](int, float x) { last = x; });
   };

   Transport& tr = Transport::Instance();
   const float tempoBefore = (float)tr.Tempo();
   tr.SetPlaying(false);
   tr.Rewind();
   tr.SetTempo(120.0f);
   tr.SetPlaying(true);

   // --- (a) the kernel plays the node's shape at the node's rate
   {
      LFONode lfo;
      lfo.voice = true;
      lfo.shape = 1; // triangle
      lfo.rateMode = LFONode::kRateBeats;
      lfo.rateBeats = 2.0f; // 1 cycle a second at 120
      lfo.phase = 0.1f;
      lfo.low = 0.2f;
      lfo.high = 0.9f;
      VoiceMod::Spec sp;
      check("LFO with per voice on gives a spec", lfo.VoiceSpec(sp) ? 1 : 0, 1, 0);
      oneRoute(sp);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(0);
      const double seconds = 0.37;
      run(0, (int)std::lround(seconds * kSr));
      for (int i = 0; i < 74; i++)
         tr.Tick(0.005f);
      check("kernel LFO = node LFO, 0.37 s from start", rt->Peek(0, 0), lfo.Value01(), 2e-4);
      lfo.voice = false;
      check("per voice off gives no spec", lfo.VoiceSpec(sp) ? 1 : 0, 0, 0);
   }

   // --- (b) Random: each note draws its own sequence, inside low..high
   {
      RandomNode rnd;
      rnd.voice = true;
      rnd.low = 0.25f;
      rnd.high = 0.75f;
      VoiceMod::Spec sp;
      rnd.VoiceSpec(sp);
      oneRoute(sp);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(0);
      rt->NoteOn(1);
      bool inside = true, differ = false;
      for (int step = 0; step < 16; step++)
      {
         run(0, 4000);
         run(1, 4000);
         const float a = rt->Peek(0, 0), b = rt->Peek(1, 0);
         inside &= a >= 0.25f && a <= 0.75f && b >= 0.25f && b <= 0.75f;
         differ |= std::fabs(a - b) > 1e-3f;
      }
      check("Random stays inside low..high", inside ? 1 : 0, 1, 0);
      check("Random: two notes draw different values", differ ? 1 : 0, 1, 0);
   }

   // --- (c) MSEG: a ramp per voice, held loops, release plays out
   {
      MSEGNode env;
      env.voice = true;
      env.points = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
      env.rateMode = LFONode::kRateHz;
      env.rateHz = 1.0f; // one shape a second
      env.loop = true;
      env.loopStart = 0.25f;
      env.loopEnd = 0.75f;
      VoiceMod::Spec sp;
      env.VoiceSpec(sp);
      oneRoute(sp);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(0);
      run(0, 12000); // 0.25 s
      rt->NoteOn(1);
      run(0, 12000);
      run(1, 12000);
      check("MSEG voice 0 at 0.5 s", rt->Peek(0, 0), 0.5, 1e-3);
      check("MSEG voice 1, started 0.25 s later, at 0.25", rt->Peek(1, 0), 0.25, 1e-3);
      run(0, 24000); // 1.0 s held: 0.25 + fmod(0.75, 0.5) = 0.5
      check("MSEG held 1 s loops 0.25-0.75", rt->Peek(0, 0), 0.5, 1e-3);
      rt->NoteOff(0);
      run(0, 48000);
      check("MSEG released plays out to the end", rt->Peek(0, 0), 1.0, 1e-6);
      check("MSEG voice 1 is untouched by voice 0's release", rt->Peek(1, 0), 0.25, 1e-3);
   }

   // --- (d) route shaping matches CookFrame: curve, lo/hi, step, clamp
   {
      MSEGNode flat;
      flat.voice = true;
      flat.points = { { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
      VoiceMod::Spec sp;
      flat.VoiceSpec(sp);
      oneRoute(sp);
      VoiceMod::Route& r = plan->routes[0];
      r.curve = 1.0f / 3.0f; // pow(0.5, 2) = 0.25
      r.lo = 0.0f;
      r.hi = 1.0f;
      r.minValue = 0.0f;
      r.maxValue = 1.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(2);
      run(2, 1);
      check("curve 1/3 bends 0.5 to 0.25", last, 0.25, 1e-5);
      r.lo = 100.0f;
      r.hi = 1000.0f;
      r.maxValue = 2000.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(2, 1);
      check("lo/hi 100..1000 maps 0.25 to 325", last, 325.0, 1e-3);
      r.step = 50.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(2, 1);
      check("step 50 snaps 325 to 350", last, 350.0, 1e-3);
      r.maxValue = 300.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(2, 1);
      check("the param's max clamps it to 300", last, 300.0, 1e-3);
      r.lo = 0.0f;
      r.maxValue = 2000.0f;
      r.step = 0.0f;
      r.curve = 0.0f;
      r.smoothMs = 10.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      rt->NoteOn(3); // a note starts its smoothing on the value, not from 0
      run(3, 1);
      check("smoothing starts on the value at note-on", last, 500.0, 1e-3);
      plan->sources[0].points[0].y = 1.0f;
      plan->sources[0].points[1].y = 1.0f;
      box->Publish(*plan);
      rt->BeginBlock(*box, 120.0);
      run(3, 480); // 10 ms = one tau: 1 - 1/e of the way from 500 to 1000
      check("smoothing: one tau covers 1-1/e", last, 1000.0 - 500.0 * std::exp(-1.0), 1.5);
      box->Publish(VoiceMod::Plan());
      rt->BeginBlock(*box, 120.0);
      check("an empty plan turns the runtime off", rt->Active() ? 1 : 0, 0, 0);
   }

   // --- (e) Analog: a real synth runs a plan per voice
   {
      AnalogNode syn;
      check("Analog cutoff is a voice param", syn.VoiceParamFor(&syn.cutoff) >= 0 ? 1 : 0, 1, 0);
      check("Analog volume is not a voice param", syn.VoiceParamFor(&syn.volume), -1, 0);
      const int numFrames = 480;
      auto render = [&](bool withPlan, std::vector<float>& out) {
         AnalogNode n;
         n.attack = 1.0f;
         n.sustain = 1.0f;
         AudioNode* audio = n.GetAudioNode();
         audio->PrepareToPlay(kSr, numFrames);
         n.CookIfNeeded(1);
         if (withPlan)
         {
            LFONode lfo;
            lfo.voice = true;
            lfo.rateMode = LFONode::kRateHz;
            lfo.rateHz = 2.0f;
            VoiceMod::Plan p;
            p.numSources = 1;
            lfo.VoiceSpec(p.sources[0]);
            p.sources[0].id = 77;
            p.numRoutes = 1;
            p.routes[0].param = n.VoiceParamFor(&n.cutoff);
            p.routes[0].lo = 200.0f;
            p.routes[0].hi = 4000.0f;
            p.routes[0].minValue = 20.0f;
            p.routes[0].maxValue = 20000.0f;
            n.PublishVoicePlan(p);
         }
         NoteEventQueue inbox;
         audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
         std::vector<float> c0(numFrames), c1(numFrames);
         float* chans[2] = { c0.data(), c1.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = numFrames;
         auto blocks = [&](int count) {
            for (int b = 0; b < count; b++)
            {
               audio->ProcessBlock(nullptr, 0, buf);
               out.insert(out.end(), c0.begin(), c0.end());
            }
         };
         NoteEvent on;
         on.note = 48;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.voiceId = 1;
         inbox.Push(on);
         blocks(10); // 0.1 s
         on.note = 55;
         on.voiceId = 2;
         inbox.Push(on);
         blocks(20); // voice 0 is 0.3 s in, voice 1 0.2 s
         if (withPlan)
         {
            // sine at 0.6 and 0.4 cycles
            check("Analog voice 0 runs its own LFO (0.6 cycles)", n.DebugVoiceSource(0, 0),
                  0.5 + 0.5 * std::sin(2.0 * M_PI * 0.6), 0.02);
            check("Analog voice 1 runs its own LFO (0.4 cycles)", n.DebugVoiceSource(1, 0),
                  0.5 + 0.5 * std::sin(2.0 * M_PI * 0.4), 0.02);
         }
      };
      std::vector<float> plain, modded;
      render(false, plain);
      render(true, modded);
      double diff = 0.0, energy = 0.0;
      for (size_t i = 0; i < plain.size() && i < modded.size(); i++)
      {
         diff += (plain[i] - modded[i]) * (plain[i] - modded[i]);
         energy += plain[i] * plain[i];
      }
      check("Analog with a plan sounds different (diff/energy > 0.05)", diff / std::max(1e-12, energy) > 0.05 ? 1 : 0, 1, 0);
      bool finite = true;
      for (float x : modded)
         finite &= std::isfinite(x);
      check("Analog with a plan stays finite", finite ? 1 : 0, 1, 0);
   }

   // --- (f) Wavetable: each engine's knobs are distinct voice params
   {
      WavetableNode wt;
      const int e0 = wt.VoiceParamFor(&wt.engines[0].cutoff), e1 = wt.VoiceParamFor(&wt.engines[1].cutoff);
      check("Wavetable engine cutoffs are voice params", e0 >= 0 && e1 >= 0 ? 1 : 0, 1, 0);
      check("Wavetable engine 1's cutoff is its own param", e0 != e1 ? 1 : 0, 1, 0);
      check("Wavetable master volume is not a voice param", wt.VoiceParamFor(&wt.volume), -1, 0);
      // An engine's level driven per voice from a square LFO: the render
      // goes through the per-voice SmoothedBlock copy, so it must differ
      // from the shared render and stay finite.
      auto render = [&](bool withPlan) {
         WavetableNode n;
         n.engines[0].on = true;
         n.engines[0].ampAttack = 1.0f;
         n.engines[0].ampSustain = 1.0f;
         n.engines[1].on = false;
         AudioNode* audio = n.GetAudioNode();
         audio->PrepareToPlay(kSr, 480);
         n.CookIfNeeded(1);
         if (withPlan)
         {
            LFONode lfo;
            lfo.voice = true;
            lfo.shape = 4; // square
            lfo.rateMode = LFONode::kRateHz;
            lfo.rateHz = 5.0f;
            VoiceMod::Plan p;
            p.numSources = 1;
            lfo.VoiceSpec(p.sources[0]);
            p.sources[0].id = 78;
            p.numRoutes = 1;
            p.routes[0].param = n.VoiceParamFor(&n.engines[0].volume);
            p.routes[0].lo = 0.0f;
            p.routes[0].hi = 0.05f;
            p.routes[0].maxValue = 1.0f;
            n.PublishVoicePlan(p);
         }
         NoteEventQueue inbox;
         audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
         std::vector<float> c0(480), c1(480), out;
         float* chans[2] = { c0.data(), c1.data() };
         AudioBuffer buf;
         buf.channels = chans;
         buf.numChannels = 2;
         buf.numFrames = 480;
         NoteEvent on;
         on.note = 57;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.voiceId = 1;
         inbox.Push(on);
         for (int b = 0; b < 30; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            out.insert(out.end(), c0.begin(), c0.end());
         }
         return out;
      };
      const std::vector<float> plain = render(false), modded = render(true);
      double diff = 0.0, energy = 0.0;
      bool finite = true;
      for (size_t i = 0; i < plain.size() && i < modded.size(); i++)
      {
         diff += (plain[i] - modded[i]) * (plain[i] - modded[i]);
         energy += plain[i] * plain[i];
         finite &= std::isfinite(modded[i]);
      }
      check("Wavetable sounds (shared render energy > 0)", energy > 1e-6 ? 1 : 0, 1, 0);
      check("Wavetable with a plan sounds different", diff / std::max(1e-12, energy) > 0.05 ? 1 : 0, 1, 0);
      check("Wavetable with a plan stays finite", finite ? 1 : 0, 1, 0);
   }

   // --- (h) Voice Spread (2.13): the four spreads, and a chord's notes
   //     each taking their place among the held keys
   {
      using namespace ModShapes;
      check("spread linear, 4: note 2 at 2/3", Spread(kSpreadLinear, 2, 4), 2.0 / 3.0, 1e-6);
      check("spread linear wraps after count (note 5 of 4 = note 1)", Spread(kSpreadLinear, 5, 4), 1.0 / 3.0, 1e-6);
      check("spread golden: note 1 at 0.618", Spread(kSpreadGolden, 1, 8), 0.6180340, 1e-6);
      check("spread golden: note 2 at 0.236", Spread(kSpreadGolden, 2, 8), 0.2360680, 1e-6);
      check("spread primes, 4: note 2 = (5-2)/(7-2)", Spread(kSpreadPrimes, 2, 4), 0.6, 1e-6);
      check("spread primes, 4: last note at 1", Spread(kSpreadPrimes, 3, 4), 1.0, 1e-6);
      check("spread alternate, 4: note 0 left of centre", Spread(kSpreadAlternate, 0, 4), 0.25, 1e-6);
      check("spread alternate, 4: note 1 right of centre", Spread(kSpreadAlternate, 1, 4), 0.75, 1e-6);
      check("spread alternate, 4: note 3 at the right edge", Spread(kSpreadAlternate, 3, 4), 1.0, 1e-6);
      check("spread of one note is the centre", Spread(kSpreadGolden, 3, 1), 0.5, 1e-6);
      bool inside = true;
      for (int m = 0; m < 4; m++)
         for (int c = 1; c <= kSpreadMaxCount; c++)
            for (int i = 0; i < 40; i++)
            {
               const float v = Spread(m, i, c);
               inside &= v >= 0.0f && v <= 1.0f;
            }
      check("every spread stays in 0..1", inside ? 1 : 0, 1, 0);

      VoiceSpreadNode sp;
      sp.mode = kSpreadLinear;
      sp.count = 3;
      sp.low = 0.2f;
      sp.high = 0.8f;
      VoiceMod::Spec spec;
      check("Voice Spread is always per voice", sp.VoiceSpec(spec) ? 1 : 0, 1, 0);
      check("Voice Spread shared value = first note's", sp.Value01(), 0.2, 1e-6);
      oneRoute(spec);
      auto rs = std::make_unique<Rt>();
      rs->Prepare(kSr);
      rs->BeginBlock(*box, 120.0);
      rs->NoteOn(5); // a chord lands on whatever slots are free
      rs->NoteOn(2);
      rs->NoteOn(7);
      check("chord note 1 (slot 5) at low", rs->Peek(5, 0), 0.2, 1e-6);
      check("chord note 2 (slot 2) in the middle", rs->Peek(2, 0), 0.5, 1e-6);
      check("chord note 3 (slot 7) at high", rs->Peek(7, 0), 0.8, 1e-6);
      rs->NoteOff(2);
      rs->NoteOn(0); // two keys held: the new one is the third
      check("a note played over two held keys is third", rs->Peek(0, 0), 0.8, 1e-6);
      check("held notes keep their place", rs->Peek(5, 0), 0.2, 1e-6);
      rs->NoteOn(5); // the same key again, still held by itself only: two others held
      check("a retriggered key counts the others, not itself", rs->Peek(5, 0), 0.8, 1e-6);
   }

   // --- (i) Envelope (2.14): AHDSR with per-stage curves. Hold 0 and
   // curves 0 is the straight ADSR the node had; per voice, each voice is
   // gated by its own note-on and note-off.
   {
      ModShapes::Ahdsr e;
      e.attackMs = 10.0f;
      e.holdMs = 0.0f;
      e.decayMs = 40.0f;
      e.sustain = 0.5f;
      e.releaseMs = 20.0f;
      auto at = [&](const ModShapes::Ahdsr& env, double sec) {
         int stage = ModShapes::kEnvAttack;
         double start = 0.0;
         float from = 0.0f;
         return ModShapes::AhdsrLevel(env, stage, start, from, sec);
      };
      check("straight attack halfway", at(e, 0.005), 0.5, 1e-5);
      check("straight decay halfway", at(e, 0.030), 0.75, 1e-5);
      check("sustain", at(e, 0.5), 0.5, 1e-6);
      e.holdMs = 15.0f;
      check("hold keeps the peak", at(e, 0.020), 1.0, 1e-6);
      check("decay starts after the hold", at(e, 0.045), 0.75, 1e-5);
      e.decayCurve = 0.5f;
      check("curved decay = MsegSegment's bend", at(e, 0.045), ModShapes::MsegSegment(1.0f, 0.5f, 0.5f, 0.5f), 1e-6);
      check("positive decay curve falls fast first", at(e, 0.045) < 0.75 ? 1 : 0, 1, 0);
      {
         int stage = ModShapes::kEnvRelease;
         double start = 0.0;
         float from = 0.8f;
         e.releaseCurve = -0.5f;
         check("release starts from its level, curved",
               ModShapes::AhdsrLevel(e, stage, start, from, 0.010), ModShapes::MsegSegment(0.8f, 0.0f, -0.5f, 0.5f), 1e-6);
      }

      EnvelopeNode env;
      VoiceMod::Spec spec;
      check("Envelope shared by default", env.VoiceSpec(spec) ? 1 : 0, 0, 0);
      env.voice = true;
      env.output = EnvelopeNode::kOutCentred;
      check("centred output stays shared", env.VoiceSpec(spec) ? 1 : 0, 0, 0);
      env.output = EnvelopeNode::kOutAdsr;
      env.attackMs = 10.0f;
      env.holdMs = 5.0f;
      env.decayMs = 50.0f;
      env.sustainLevel = 0.5f;
      env.releaseMs = 20.0f;
      env.attackCurve = env.decayCurve = env.releaseCurve = 0.0f;
      check("Envelope per voice with ADSR output", env.VoiceSpec(spec) ? 1 : 0, 1, 0);
      EnvelopeNode fresh;
      check("a new Envelope's decay falls fast first", fresh.decayCurve > 0.0f ? 1 : 0, 1, 0);
      fresh.BeforeLoadParams();
      check("an older patch loads straight", fresh.decayCurve + fresh.releaseCurve + fresh.holdMs, 0.0, 0.0);

      oneRoute(spec);
      auto re = std::make_unique<Rt>();
      re->Prepare(kSr);
      re->BeginBlock(*box, 120.0);
      auto runOn = [&](Rt& r, int voice, double ms) {
         for (int i = 0, n = (int)std::lround(ms * kSr / 1000.0); i < n; i++)
            r.Apply(voice, [&](int, float x) { last = x; });
      };
      re->NoteOn(3);
      runOn(*re, 3, 5.0);
      check("voice 3 attack halfway at 5 ms", re->Peek(3, 0), 0.5, 2e-3);
      check("voice 0, never played, at rest", re->Peek(0, 0), 0.0, 0.0);
      runOn(*re, 3, 7.0);
      check("voice 3 holds at 12 ms", re->Peek(3, 0), 1.0, 1e-6);
      runOn(*re, 3, 28.0);
      check("voice 3 decay halfway at 40 ms", re->Peek(3, 0), 0.75, 2e-3);
      runOn(*re, 3, 60.0);
      check("voice 3 sustains", re->Peek(3, 0), 0.5, 1e-6);
      re->NoteOn(6);
      runOn(*re, 6, 5.0);
      check("voice 6 starts its own attack", re->Peek(6, 0), 0.5, 2e-3);
      check("voice 3 still sustains", re->Peek(3, 0), 0.5, 1e-6);
      re->NoteOff(3);
      runOn(*re, 3, 10.0);
      check("voice 3 release halfway after its note-off", re->Peek(3, 0), 0.25, 2e-3);
      const float before = re->Peek(3, 0);
      re->NoteOn(3);
      check("a retrigger starts the attack where release was", re->Peek(3, 0), before, 2e-3);
      re->NoteOff(3);
      runOn(*re, 3, 30.0);
      check("released to rest", re->Peek(3, 0), 0.0, 1e-6);
   }

   // --- (j) Note Expression (2.15): each voice follows its own note's MPE
   // pressure or slide, from the note-on and through Express, smoothed.
   {
      NoteExpressionNode ne;
      ne.low = 0.2f;
      ne.high = 0.8f;
      VoiceMod::Spec spec;
      check("Note Expression is always per voice", ne.VoiceSpec(spec) ? 1 : 0, 1, 0);
      oneRoute(spec);
      auto rx = std::make_unique<Rt>();
      rx->Prepare(kSr);
      rx->BeginBlock(*box, 120.0);
      auto runOn = [&](Rt& r, int voice, double ms) {
         for (int i = 0, n = (int)std::lround(ms * kSr / 1000.0); i < n; i++)
            r.Apply(voice, [&](int, float x) { last = x; });
      };
      rx->NoteOn(1, 0.5f, 0.1f);
      rx->NoteOn(4, 1.0f, 0.9f);
      check("voice 1 starts at its note's pressure (no glide in)", rx->Peek(1, 0), 0.2 + 0.6 * 0.5, 1e-6);
      check("voice 4 starts at its own", rx->Peek(4, 0), 0.8, 1e-6);
      runOn(*rx, 1, 1.0);
      rx->Express(1, 0.0f, 0.1f);
      runOn(*rx, 1, 5.0);
      check("a pressure move is smoothed: one time constant in (e^-1 left)", rx->Peek(1, 0),
            0.2 + 0.6 * 0.5 * std::exp(-1.0), 0.01);
      runOn(*rx, 1, 50.0);
      check("and settles", rx->Peek(1, 0), 0.2, 1e-3);
      check("voice 4 does not move with voice 1", rx->Peek(4, 0), 0.8, 1e-6);
      rx->NoteOn(1, 0.9f, 0.0f);
      check("a new note on the slot starts at its own value", rx->Peek(1, 0), 0.2 + 0.6 * 0.9, 1e-6);

      ne.source = VoiceMod::kExprSlide;
      ne.VoiceSpec(spec);
      oneRoute(spec);
      auto rs2 = std::make_unique<Rt>();
      rs2->Prepare(kSr);
      rs2->BeginBlock(*box, 120.0);
      rs2->NoteOn(2, 1.0f, 0.25f);
      check("slide source reads slide, not pressure", rs2->Peek(2, 0), 0.2 + 0.6 * 0.25, 1e-6);

      // Analog end to end: two notes with their own pressure; a Pitch Bend
      // style bendUpdate (no expression) leaves pressure where it was.
      AnalogNode syn;
      syn.attack = 1.0f;
      syn.sustain = 1.0f;
      AudioNode* audio = syn.GetAudioNode();
      audio->PrepareToPlay(kSr, 480);
      syn.CookIfNeeded(1);
      VoiceMod::Plan p;
      p.numSources = 1;
      NoteExpressionNode press;
      press.VoiceSpec(p.sources[0]);
      p.sources[0].id = 91;
      p.numRoutes = 1;
      p.routes[0].param = syn.VoiceParamFor(&syn.cutoff);
      p.routes[0].lo = 200.0f;
      p.routes[0].hi = 4000.0f;
      p.routes[0].minValue = 20.0f;
      p.routes[0].maxValue = 20000.0f;
      syn.PublishVoicePlan(p);
      NoteEventQueue inbox;
      audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
      std::vector<float> c0(480), c1(480);
      float* chans[2] = { c0.data(), c1.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = 480;
      bool finite = true;
      auto blocks = [&](int count) {
         for (int b = 0; b < count; b++)
         {
            audio->ProcessBlock(nullptr, 0, buf);
            for (float x : c0)
               finite &= std::isfinite(x);
         }
      };
      NoteEvent on;
      on.isNoteOn = true;
      on.velocity = 1.0f;
      on.hasExpression = true;
      on.note = 48;
      on.voiceId = 1;
      on.pressure = 0.25f;
      inbox.Push(on);
      on.note = 55;
      on.voiceId = 2;
      on.pressure = 0.75f;
      inbox.Push(on);
      blocks(2);
      check("Analog voice 0 follows its note's pressure", syn.DebugVoiceSource(0, 0), 0.25, 1e-3);
      check("Analog voice 1 follows its own", syn.DebugVoiceSource(1, 0), 0.75, 1e-3);
      NoteEvent ex;
      ex.bendUpdate = true;
      ex.hasExpression = true;
      ex.voiceId = 1;
      ex.note = 48;
      ex.pressure = 1.0f;
      inbox.Push(ex);
      blocks(5); // 50 ms: ten time constants
      check("an expression update moves voice 0 only", syn.DebugVoiceSource(0, 0), 1.0, 1e-3);
      check("voice 1 unchanged", syn.DebugVoiceSource(1, 0), 0.75, 1e-3);
      NoteEvent bend;
      bend.bendUpdate = true;
      bend.voiceId = 2;
      bend.note = 55;
      bend.bendSemitones = 2.0f;
      inbox.Push(bend);
      blocks(2);
      check("a bendUpdate with no expression keeps the note's pressure", syn.DebugVoiceSource(1, 0), 0.75, 1e-3);
      check("Analog with Note Expression stays finite", finite ? 1 : 0, 1, 0);
   }

   // --- (g) the switch saves
   {
      struct BoolGrab : ParamVisitor
      {
         bool* target = nullptr;
         bool set = false;
         bool value = false;
         void Float(const char*, float&) override {}
         void Int(const char*, int&) override {}
         void Text(const char*, std::string&) override {}
         void Color(const char*, float*) override {}
         void Bool(const char* name, bool& v) override
         {
            if (std::strcmp(name, "voice") != 0)
               return;
            if (set)
               v = value;
            else
               value = v;
         }
      };
      LFONode a, b;
      a.voice = true;
      BoolGrab g;
      a.VisitParams(g);
      g.set = true;
      b.VisitParams(g);
      check("LFO per voice survives save/load", b.voice ? 1 : 0, 1, 0);
      MSEGNode m;
      BoolGrab gm;
      m.VisitParams(gm);
      check("MSEG per voice defaults off", gm.value ? 1 : 0, 0, 0);
   }

   tr.SetTempo(tempoBefore);
   printf("VOICEMODTEST %s\n", all ? "OK" : "FAIL");
   return all;
}

// ============================================== INFINITE_SYNTHSIMDTEST
// 2.16: each synth has a fast voice path - Analog runs four voices to a SIMD
// lane group, Wavetable caches its per-sample math on exact inputs - and a
// reference path (VoiceSimd::ForceScalar). Renders an 8-note chord through
// each synth's audio half both ways and checks (1) the two agree to -120 dB
// and (2) the render cost per voice (ms of CPU per second of audio, chord
// minus idle) against the reference.
namespace
{
   template <class N>
   double SynthRenderMsPerSecond(int voices, double seconds, bool scalar, std::vector<float>* capture,
                                 void (*setup)(N&) = nullptr)
   {
      VoiceSimd::ForceScalar().store(scalar, std::memory_order_relaxed);
      N n;
      if (setup)
         setup(n);
      AudioNode* audio = n.GetAudioNode();
      const int block = 256;
      audio->PrepareToPlay(48000.0, block);
      n.CookIfNeeded(1);
      NoteEventQueue inbox;
      audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());
      static const int kChord[8] = { 48, 52, 55, 59, 60, 64, 67, 71 };
      for (int v = 0; v < voices; v++)
      {
         NoteEvent on;
         on.note = kChord[v];
         on.velocity = 0.7f;
         on.isNoteOn = true;
         on.voiceId = 100 + v;
         inbox.Push(on);
      }
      std::vector<float> l(block), r(block);
      float* chans[2] = { l.data(), r.data() };
      AudioBuffer buf;
      buf.channels = chans;
      buf.numChannels = 2;
      buf.numFrames = block;
      const int blocks = (int)(seconds * 48000.0 / block);
      const auto t0 = std::chrono::steady_clock::now();
      for (int b = 0; b < blocks; b++)
      {
         audio->ProcessBlock(nullptr, 0, buf);
         if (capture)
            for (int i = 0; i < block; i++)
            {
               capture->push_back(l[(size_t)i]);
               capture->push_back(r[(size_t)i]);
            }
      }
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      VoiceSimd::ForceScalar().store(false, std::memory_order_relaxed);
      return ms / seconds;
   }

   // Lanes and scalar render the same 8-note chord; the difference, relative
   // to the signal, must sit at or below -120 dB.
   template <class N>
   bool SynthSimdClose(const char* name, const char* variant, void (*setup)(N&))
   {
      std::vector<float> lanes, scalar;
      SynthRenderMsPerSecond<N>(8, 2.0, false, &lanes, setup);
      SynthRenderMsPerSecond<N>(8, 2.0, true, &scalar, setup);
      double err = 0.0, ref = 0.0;
      for (size_t i = 0; i < lanes.size() && i < scalar.size(); i++)
      {
         err += ((double)lanes[i] - scalar[i]) * ((double)lanes[i] - scalar[i]);
         ref += (double)scalar[i] * scalar[i];
      }
      const double db = 10.0 * std::log10(std::max(1e-30, err) / std::max(1e-30, ref));
      const bool close = db <= -120.0 && ref > 0.0 && lanes.size() == scalar.size();
      printf("SYNTHSIMDTEST %-9s %-28s fast vs reference %.1f dB (<= -120) %s\n", name, variant, db, close ? "OK" : "FAIL");
      return close;
   }

   template <class N>
   bool SynthSimdCase(const char* name, double maxRatio)
   {
      bool ok = SynthSimdClose<N>(name, "default", nullptr);
      // Best of three, so a busy machine reads its quietest run.
      double best[2] = { 1e30, 1e30 };
      for (int pass = 0; pass < 3; pass++)
         for (int s = 0; s < 2; s++)
         {
            const double idle = SynthRenderMsPerSecond<N>(0, 2.0, s == 1, nullptr);
            const double busy = SynthRenderMsPerSecond<N>(8, 2.0, s == 1, nullptr);
            best[s] = std::min(best[s], (busy - idle) / 8.0);
         }
      const double ratio = best[0] / std::max(1e-9, best[1]);
      const bool fast = ratio <= maxRatio;
      printf("SYNTHSIMDTEST %-9s per voice %.2f ms/s fast path, %.2f reference (x%.2f, limit x%.2f) %s\n", name, best[0],
             best[1], ratio, maxRatio, fast ? "OK" : "FAIL");
      ok = ok && fast;
      return ok;
   }
}

bool RunSynthSimdFixture()
{
   bool ok = true;
   // INFINITE_SYNTHSIMDTEST=Wavetable (or Analog) runs that synth only, for profiling.
   const char* only = getenv("INFINITE_SYNTHSIMDTEST");
   const bool all = only == nullptr || strcmp(only, "1") == 0;
   const bool analog = all || strcmp(only, "Analog") == 0;
   const bool wavetable = all || strcmp(only, "Wavetable") == 0;
   // INFINITE_SYNTHSIMDPROFILE=<seconds>: just render that much of the chord
   // on the lane path, for `sample` / Instruments, and exit.
   if (const char* prof = getenv("INFINITE_SYNTHSIMDPROFILE"))
   {
      const double seconds = std::max(1.0, atof(prof));
      if (analog)
         printf("SYNTHSIMDTEST Analog %.2f ms/s\n", SynthRenderMsPerSecond<AnalogNode>(8, seconds, false, nullptr));
      if (wavetable)
         printf("SYNTHSIMDTEST Wavetable %.2f ms/s\n", SynthRenderMsPerSecond<WavetableNode>(8, seconds, false, nullptr));
      return true;
   }
   if (analog)
   {
   // Speed is gated as fast path / reference path on the same machine, so it
   // holds on any CI box. The 2.16 target (-50% per voice) is measured by
   // synth_probe on the whole app: 2026-10-14, Analog 18.82 -> 6.49 ms/s,
   // Wavetable 3.37 -> 1.37. Here (audio half alone, idle subtracted) Analog
   // reads about x0.44 and Wavetable about x0.52, so the limits catch a lost
   // fast path without flaking on a noisy run.
   ok = SynthSimdCase<AnalogNode>("Analog", 0.6) && ok;
   // The paths where lanes and scalar could drift apart: a self-oscillating
   // driven ladder on a wide stack (both sides), and each SVF shape.
   ok = SynthSimdClose<AnalogNode>("Analog", "ladder res 1 drive wide", [](AnalogNode& n) {
      n.resonance = 1.0f;
      n.drive = 0.7f;
      n.cutoff = 2000.0f;
      n.voices = 4.0f;
      n.spread = 0.9f;
   }) && ok;
   ok = SynthSimdClose<AnalogNode>("Analog", "svf lp 36 res 0.8", [](AnalogNode& n) {
      n.filterType = kAFilterSvfLP36;
      n.resonance = 0.8f;
      n.cutoff = 1500.0f;
   }) && ok;
   ok = SynthSimdClose<AnalogNode>("Analog", "svf hp 24 wide", [](AnalogNode& n) {
      n.filterType = kAFilterSvfHP24;
      n.resonance = 0.5f;
      n.voices = 2.0f;
      n.spread = 1.0f;
   }) && ok;
   ok = SynthSimdClose<AnalogNode>("Analog", "svf bp 24", [](AnalogNode& n) {
      n.filterType = kAFilterSvfBP24;
      n.resonance = 0.6f;
   }) && ok;
   ok = SynthSimdClose<AnalogNode>("Analog", "svf notch 12", [](AnalogNode& n) { n.filterType = kAFilterSvfNotch12; }) && ok;
   ok = SynthSimdClose<AnalogNode>("Analog", "filter off", [](AnalogNode& n) { n.filterType = kAFilterOff; }) && ok;
   // v0.6 2.25: the MS-20 and diode models, self-oscillating and driven.
   ok = SynthSimdClose<AnalogNode>("Analog", "ms-20 res 1 drive wide", [](AnalogNode& n) {
      n.filterType = kAFilterMs20;
      n.resonance = 1.0f;
      n.drive = 0.7f;
      n.voices = 4.0f;
      n.spread = 0.9f;
   }) && ok;
   ok = SynthSimdClose<AnalogNode>("Analog", "diode res 1 drive", [](AnalogNode& n) {
      n.filterType = kAFilterDiode;
      n.resonance = 1.0f;
      n.drive = 0.7f;
      n.cutoff = 1200.0f;
   }) && ok;
   }
   if (wavetable)
   {
      ok = SynthSimdCase<WavetableNode>("Wavetable", 0.7) && ok;
      // Each cache keyed on an input that moves here: a wide unison stack
      // (detune, pan, frames), a filter swept by its envelope, a pitch
      // envelope (carrier), a phase warp (mip crossfade), engine B with a
      // cross-mod warp (the operator sine), a comb filter, and glide.
      ok = SynthSimdClose<WavetableNode>("Wavetable", "unison 6 wide moving", [](WavetableNode& n) {
         n.engines[0].unison = 6;
         n.engines[0].detune = 30.0f;
         n.engines[0].stereoWidth = 1.0f;
         n.engines[0].position = 0.37f;
         n.engines[0].phaseRandomize = 0.5f;
      }) && ok;
      ok = SynthSimdClose<WavetableNode>("Wavetable", "lp 24 env sweep + pitch env", [](WavetableNode& n) {
         n.engines[0].filterType = SynthModes::kFilterLP24;
         n.engines[0].cutoff = 400.0f;
         n.engines[0].resonance = 0.7f;
         n.engines[0].filterAmount = 5.0f;
         n.engines[0].pitchAmount = 12.0f;
      }) && ok;
      ok = SynthSimdClose<WavetableNode>("Wavetable", "sync warp", [](WavetableNode& n) {
         n.engines[0].warpMode = SynthModes::kWarpSync;
         n.engines[0].warpAmount = 0.6f;
         n.engines[0].unison = 3;
      }) && ok;
      ok = SynthSimdClose<WavetableNode>("Wavetable", "engine b fm + mix", [](WavetableNode& n) {
         n.engines[1].on = true;
         n.engines[1].octave = 1;
         n.engines[0].warpMode = SynthModes::kWarpFM;
         n.engines[0].warpAmount = 0.4f;
         n.engines[1].warpMode = SynthModes::kWarpRM;
         n.engines[1].warpAmount = 0.5f;
         n.mix = 0.4f;
      }) && ok;
      ok = SynthSimdClose<WavetableNode>("Wavetable", "operator fm, comb, glide", [](WavetableNode& n) {
         n.engines[1].on = false;
         n.engines[0].warpMode = SynthModes::kWarpPD;
         n.engines[0].warpAmount = 0.5f;
         n.engines[0].warpRatio = 2.0f;
         n.engines[0].filterType = SynthModes::kFilterCombPos;
         n.engines[0].resonance = 0.5f;
         n.engines[0].filterAmount = -2.0f;
         n.glide = 0.05f;
      }) && ok;
   }
   printf("SYNTHSIMDTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

// ============================================== INFINITE_SATURATORTEST
// 2.17: Saturator's routing graph, against facts that follow from it:
//   - every stage off is the dry path plus the wet DC blocker (the null
//     test), in each routing but
//     feedback - 3-band included, whose bands sum back to the same allpass
//     the dry path runs;
//   - the 3-band split is flat (it is an allpass) from 40 Hz to 16 kHz,
//     measured against serial so the shared DC blocker (-0.17 dB at 40 Hz)
//     and oversampler drop out;
//   - mid-side keeps a mono input mono, curves on;
//   - feedback changes the sound and stays bounded at its maximum with every
//     stage at full amount;
//   - at amount 0 and -40 dBFS each shaping curve is close to linear
//     (rectify is excluded: it is not linear at any level).
namespace
{
   const EffectDef* SaturatorDef()
   {
      for (const EffectDef& d : GetEffectDefs())
         if (d.name == "Saturator")
            return &d;
      return nullptr;
   }

   std::unique_ptr<AudioEffectNode> MakeSaturator(const std::vector<std::pair<const char*, float>>& params, float mix,
                                                  double sampleRate)
   {
      auto node = std::make_unique<AudioEffectNode>(*SaturatorDef());
      node->mix = mix;
      for (const auto& [name, v] : params)
         *node->ParamPtr(name) = v;
      node->GetAudioNode()->PrepareToPlay(sampleRate, 512);
      node->CookIfNeeded(1);
      return node;
   }

   std::vector<std::vector<float>> RenderSaturator(AudioEffectNode& node, int frames, int channels,
                                                    const std::function<float(int, int)>& in)
   {
      std::vector<std::vector<float>> out;
      DelayTest::RunSamples(node, frames, channels, in, &out);
      return out;
   }

   // Energy of a - b over energy of b, from `from` on, in dB.
   double DiffDb(const std::vector<float>& a, const std::vector<float>& b, int from)
   {
      double e = 0.0, r = 0.0;
      for (size_t i = (size_t)from; i < a.size() && i < b.size(); i++)
      {
         e += (double)(a[i] - b[i]) * (a[i] - b[i]);
         r += (double)b[i] * b[i];
      }
      return 10.0 * std::log10(std::max(e, 1e-30) / std::max(r, 1e-30));
   }

   // Level of a sine of `hz` in x (from `from` on), by projection.
   double ToneAmp(const std::vector<float>& x, double hz, double sampleRate, int from)
   {
      double c = 0.0, s = 0.0;
      int n = 0;
      for (size_t i = (size_t)from; i < x.size(); i++, n++)
      {
         const double ph = 2.0 * M_PI * hz * (double)i / sampleRate;
         c += x[i] * std::cos(ph);
         s += x[i] * std::sin(ph);
      }
      return 2.0 * std::sqrt(c * c + s * s) / (double)std::max(1, n);
   }
}

bool RunSaturatorFixture()
{
   using namespace SaturatorDsp;
   const double sr = 48000.0;
   bool ok = true;
   if (SaturatorDef() == nullptr)
   {
      printf("SATURATORTEST no Saturator effect  FAIL\n");
      return false;
   }
   const int frames = 24000, warm = 4800;
   auto noise = [](int i, int ch) {
      uint32_t h = (uint32_t)i * 2654435761u + (uint32_t)ch * 40503u;
      h ^= h >> 13;
      h *= 0x5bd1e995u;
      h ^= h >> 15;
      return 0.5f * ((float)(h & 0xffff) / 32768.0f - 1.0f);
   };

   // 1. Null: stages off, wet (mix 1) against the kernel's own dry (mix 0).
   for (int routing : { (int)kSerial, (int)kParallel, (int)kMidSide, (int)kBands })
   {
      const std::vector<std::pair<const char*, float>> p = { { "model1", 0.0f }, { "routing", (float)routing } };
      auto wet = MakeSaturator(p, 1.0f, sr);
      auto dry = MakeSaturator(p, 0.0f, sr);
      const auto w = RenderSaturator(*wet, frames, 2, noise);
      auto d = RenderSaturator(*dry, frames, 2, noise);
      // The wet path ends in a DC blocker and the dry does not (mix 0 gives
      // the input back whole), so the reference gets the same blocker.
      for (auto& ch : d)
      {
         DspMath::DcBlocker blocker;
         blocker.SetSampleRate(sr);
         for (float& v : ch)
            v = blocker.Process(v);
      }
      const double db = std::max(DiffDb(w[0], d[0], warm), DiffDb(w[1], d[1], warm));
      const bool pass = db <= -100.0;
      ok &= pass;
      printf("SATURATORTEST null, stages off, %-9s wet vs dry %7.1f dB (<= -100)  %s\n",
             RoutingList()[(size_t)routing].c_str(), db, pass ? "OK" : "FAIL");
   }

   // 2. 3-band split flat.
   {
      double worst = 0.0;
      for (double hz : { 40.0, 150.0, 250.0, 600.0, 1500.0, 2500.0, 6000.0, 16000.0 })
      {
         auto tone = [hz, sr](int i, int) { return 0.25f * (float)std::sin(2.0 * M_PI * hz * (double)i / sr); };
         auto node = MakeSaturator({ { "model1", 0.0f }, { "routing", (float)kBands } }, 1.0f, sr);
         auto serial = MakeSaturator({ { "model1", 0.0f } }, 1.0f, sr);
         const auto y = RenderSaturator(*node, frames, 1, tone);
         const auto ref = RenderSaturator(*serial, frames, 1, tone);
         const double bandsDb = 20.0 * std::log10(ToneAmp(y[0], hz, sr, warm) / 0.25);
         const double serialDb = 20.0 * std::log10(ToneAmp(ref[0], hz, sr, warm) / 0.25);
         worst = std::max(worst, std::fabs(bandsDb - serialDb));
      }
      const bool pass = worst <= 0.05;
      ok &= pass;
      printf("SATURATORTEST 3-band, stages off, 40 Hz-16 kHz within %.4f dB (<= 0.05)  %s\n", worst, pass ? "OK" : "FAIL");
   }

   // 3. Mid-side keeps mono mono.
   {
      auto node = MakeSaturator({ { "routing", (float)kMidSide },
                                  { "model1", (float)kTube }, { "amount1", 0.7f },
                                  { "model2", (float)kFold }, { "amount2", 0.7f },
                                  { "model3", (float)kDiode }, { "amount3", 0.5f } },
                                1.0f, sr);
      const auto y = RenderSaturator(*node, frames, 2, [&](int i, int) { return noise(i, 0); });
      const double db = DiffDb(y[1], y[0], warm);
      const bool pass = db <= -120.0;
      ok &= pass;
      printf("SATURATORTEST mid-side, mono in, left vs right %7.1f dB (<= -120)  %s\n", db, pass ? "OK" : "FAIL");
   }

   // 4. Feedback: changes the sound; bounded at the top of its range.
   {
      const std::vector<std::pair<const char*, float>> base = { { "model1", (float)kTube }, { "amount1", 0.3f } };
      auto serial = MakeSaturator(base, 1.0f, sr);
      auto fbBase = base;
      fbBase.push_back({ "routing", (float)kFeedback });
      fbBase.push_back({ "feedback", 0.5f });
      auto fb = MakeSaturator(fbBase, 1.0f, sr);
      const auto a = RenderSaturator(*serial, frames, 1, noise);
      const auto b = RenderSaturator(*fb, frames, 1, noise);
      const double db = DiffDb(b[0], a[0], warm);
      const bool pass = db >= -20.0;
      ok &= pass;
      printf("SATURATORTEST feedback 0.5 vs serial %7.1f dB apart (>= -20)  %s\n", db, pass ? "OK" : "FAIL");

      std::vector<std::pair<const char*, float>> hot = { { "routing", (float)kFeedback }, { "feedback", 0.95f } };
      const int models[3] = { kTube, kFold, kTape };
      static const char* kM[3] = { "model1", "model2", "model3" };
      static const char* kA[3] = { "amount1", "amount2", "amount3" };
      for (int s = 0; s < 3; s++)
      {
         hot.push_back({ kM[s], (float)models[s] });
         hot.push_back({ kA[s], 1.0f });
      }
      auto node = MakeSaturator(hot, 1.0f, sr);
      const auto y = RenderSaturator(*node, 4 * frames, 1, [](int i, int) {
         return (float)std::sin(2.0 * M_PI * 110.0 * (double)i / 48000.0);
      });
      float peak = 0.0f;
      bool finite = true;
      for (float v : y[0])
      {
         finite &= std::isfinite(v);
         peak = std::max(peak, std::fabs(v));
      }
      const bool stable = finite && peak < 8.0f;
      ok &= stable;
      printf("SATURATORTEST feedback 0.95, three stages at full: peak %.2f, %s (< 8)  %s\n", peak,
             finite ? "finite" : "NOT finite", stable ? "OK" : "FAIL");
   }

   // 5. Amount 0 at -40 dBFS: near linear.
   for (int model : { (int)kTube, (int)kTape, (int)kDiode, (int)kFold, (int)kCrush })
   {
      auto shaped = MakeSaturator({ { "model1", (float)model }, { "amount1", 0.0f } }, 1.0f, sr);
      auto off = MakeSaturator({ { "model1", 0.0f } }, 1.0f, sr);
      auto in = [](int i, int) { return 0.01f * (float)std::sin(2.0 * M_PI * 1000.0 * (double)i / 48000.0); };
      const auto a = RenderSaturator(*shaped, frames, 1, in);
      const auto b = RenderSaturator(*off, frames, 1, in);
      const double db = DiffDb(a[0], b[0], warm);
      // Diode's two sides have different knees (1 and 0.5), so its second
      // order term is the largest at small levels: about 2 dB more
      // distortion than the symmetric curves. Its gate is -35, not -40.
      const double gate = model == kDiode ? -35.0 : -40.0;
      const bool pass = db <= gate;
      ok &= pass;
      printf("SATURATORTEST %-7s amount 0, -40 dBFS: off by %7.1f dB (<= %.0f)  %s\n", ModelList()[(size_t)model].c_str(),
             db, gate, pass ? "OK" : "FAIL");
   }

   printf("SATURATORTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}
// ============================================== INFINITE_SPLITTERTEST
// 2.18: Splitter's crossover mode, against facts that follow from LR4:
//   - bands 1 is the plain copy, bit for bit;
//   - the bands sum to an allpass of the input: each tone from 20 Hz to
//     20 kHz comes back within 0.1 dB, for 2, 3 and 4 bands;
//   - each band carries its own range: a tone an octave inside a band is
//     loudest in that band;
//   - two channels split independently (a left-only tone leaves right silent).
namespace
{
   std::vector<std::vector<float>> RenderSplitter(SplitterNode& node, int frames, double sr,
                                                   const std::function<float(int, int)>& in)
   {
      AudioNode* an = node.GetAudioNode();
      an->PrepareToPlay(sr, 512);
      node.CookIfNeeded(1);
      const int bands = SplitterNode::kMaxBands;
      std::vector<std::vector<float>> out((size_t)bands * 2, std::vector<float>((size_t)frames, 0.0f));
      const int block = 256;
      std::vector<float> inL(block), inR(block);
      std::vector<std::vector<float>> o((size_t)bands * 2, std::vector<float>(block));
      for (int start = 0; start < frames; start += block)
      {
         const int n = std::min(block, frames - start);
         for (int i = 0; i < n; i++)
         {
            inL[(size_t)i] = in(start + i, 0);
            inR[(size_t)i] = in(start + i, 1);
         }
         float* inCh[2] = { inL.data(), inR.data() };
         AudioBuffer inBuf;
         inBuf.channels = inCh;
         inBuf.numChannels = 2;
         inBuf.numFrames = n;
         const AudioBuffer* ins[1] = { &inBuf };
         float* chans[SplitterNode::kMaxBands][2];
         AudioBuffer outBuf[SplitterNode::kMaxBands];
         AudioBuffer* outs[SplitterNode::kMaxBands];
         for (int b = 0; b < bands; b++)
         {
            chans[b][0] = o[(size_t)b * 2].data();
            chans[b][1] = o[(size_t)b * 2 + 1].data();
            outBuf[b].channels = chans[b];
            outBuf[b].numChannels = 2;
            outBuf[b].numFrames = n;
            outs[b] = &outBuf[b];
         }
         an->ProcessBlockMulti(ins, 1, outs, bands);
         for (size_t c = 0; c < out.size(); c++)
            std::copy(o[c].begin(), o[c].begin() + n, out[c].begin() + start);
      }
      return out;
   }
}

bool RunSplitterFixture()
{
   bool ok = true;
   const double sr = 48000.0;
   const int frames = 48000;
   const int warm = 24000;

   // 1. Copy mode is bit-identical.
   {
      SplitterNode node;
      auto in = [](int i, int ch) { return (float)std::sin(0.0123 * i + ch) * 0.7f + 0.01f * (float)(i % 7); };
      const auto out = RenderSplitter(node, 4096, sr, in);
      bool same = true, restSilent = true;
      for (int i = 0; i < 4096; i++)
      {
         same &= out[0][(size_t)i] == in(i, 0) && out[1][(size_t)i] == in(i, 1);
         for (size_t c = 2; c < out.size(); c++)
            restSilent &= out[c][(size_t)i] == 0.0f;
      }
      const bool pass = same && restSilent;
      ok &= pass;
      printf("SPLITTERTEST copy: out bit-identical %s, band pins silent %s  %s\n", same ? "yes" : "no",
             restSilent ? "yes" : "no", pass ? "OK" : "FAIL");
   }

   // 2. The sum of the bands is flat, 20 Hz - 20 kHz.
   const double tones[] = { 20.0, 40.0, 100.0, 200.0, 250.0, 315.0, 1000.0, 2500.0, 4000.0, 8000.0, 12000.0, 20000.0 };
   for (int bands = 2; bands <= SplitterNode::kMaxBands; bands++)
   {
      double worst = 0.0;
      for (double hz : tones)
      {
         SplitterNode node;
         node.bands = bands;
         auto in = [hz, sr](int i, int) { return 0.5f * (float)std::sin(2.0 * M_PI * hz * (double)i / sr); };
         const auto out = RenderSplitter(node, frames, sr, in);
         std::vector<float> sum((size_t)frames, 0.0f), ref((size_t)frames);
         for (int b = 0; b < bands; b++)
            for (int i = 0; i < frames; i++)
               sum[(size_t)i] += out[(size_t)b * 2][(size_t)i];
         for (int i = 0; i < frames; i++)
            ref[(size_t)i] = in(i, 0);
         const double db = 20.0 * std::log10(ToneAmp(sum, hz, sr, warm) / ToneAmp(ref, hz, sr, warm));
         worst = std::max(worst, std::fabs(db));
      }
      const bool pass = worst <= 0.1;
      ok &= pass;
      printf("SPLITTERTEST %d bands: sum within %.4f dB of input, 20 Hz-20 kHz (<= 0.1)  %s\n", bands, worst,
             pass ? "OK" : "FAIL");
   }

   // 3. Each band carries its own range (4 bands, 250 / 2500 / 8000 Hz).
   {
      const double probes[SplitterNode::kMaxBands] = { 80.0, 800.0, 4500.0, 16000.0 };
      bool all = true;
      for (int want = 0; want < SplitterNode::kMaxBands; want++)
      {
         SplitterNode node;
         node.bands = SplitterNode::kMaxBands;
         const double hz = probes[want];
         auto in = [hz, sr](int i, int) { return 0.5f * (float)std::sin(2.0 * M_PI * hz * (double)i / sr); };
         const auto out = RenderSplitter(node, frames, sr, in);
         int loudest = 0;
         double best = -1.0;
         for (int b = 0; b < SplitterNode::kMaxBands; b++)
         {
            const double a = ToneAmp(out[(size_t)b * 2], hz, sr, warm);
            if (a > best)
            {
               best = a;
               loudest = b;
            }
         }
         all &= loudest == want;
         printf("SPLITTERTEST %6.0f Hz loudest in band %d (want %d)  %s\n", hz, loudest + 1, want + 1,
                loudest == want ? "OK" : "FAIL");
      }
      ok &= all;
   }

   // 4. Channels split independently.
   {
      SplitterNode node;
      node.bands = 3;
      auto in = [sr](int i, int ch) { return ch == 0 ? 0.5f * (float)std::sin(2.0 * M_PI * 1000.0 * (double)i / sr) : 0.0f; };
      const auto out = RenderSplitter(node, 8192, sr, in);
      float right = 0.0f;
      for (int b = 0; b < 3; b++)
         for (float v : out[(size_t)b * 2 + 1])
            right = std::max(right, std::fabs(v));
      const bool pass = right == 0.0f;
      ok &= pass;
      printf("SPLITTERTEST left-only tone: right peak %g (== 0)  %s\n", right, pass ? "OK" : "FAIL");
   }

   printf("SPLITTERTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}
// INFINITE_FMTEST (v0.6 2.22): FM through the real node. A lone carrier
// is in tune and pure; one 1:3 modulator puts the first sidebands where
// Bessel says, at the level J1/J0 says; a fixed operator ignores the note;
// all 11 algorithms sound and stay finite; a released note ends.
bool RunFMFixture()
{
   const double sr = 48000.0;
   const int block = 256;
   bool ok = true;

   struct Rig
   {
      std::unique_ptr<FMNode> node;
      NoteEventQueue inbox;
      AudioNode* audio = nullptr;
      int frame = 0;
   };
   auto make = [&](Rig& r, const std::function<void(FMNode&)>& set) {
      r.node.reset(static_cast<FMNode*>(FMNode::Create()));
      for (auto& op : r.node->ops)
      {
         op.attack = 0.0f;
         op.sustain = 1.0f;
         op.release = 20.0f;
      }
      set(*r.node);
      r.node->CookIfNeeded(r.frame++);
      r.audio = r.node->GetAudioNode();
      r.audio->PrepareToPlay(sr, block);
      r.audio->SetNoteInbox(&r.inbox, r.inbox.RegisterConsumer());
   };
   auto note = [](Rig& r, int n, bool on) {
      NoteEvent e;
      e.note = n;
      e.velocity = on ? 1.0f : 0.0f;
      e.isNoteOn = on;
      e.voiceId = 7;
      r.inbox.Push(e);
   };
   auto render = [&](Rig& r, int frames) {
      std::vector<float> out((size_t)frames);
      std::vector<float> l(block), rr(block);
      for (int done = 0; done < frames; done += block)
      {
         float* ch[2] = { l.data(), rr.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = block;
         r.audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < block && done + i < frames; i++)
            out[(size_t)(done + i)] = l[(size_t)i];
      }
      return out;
   };
   // Amplitude of one frequency (Hann-windowed DFT bin), and the pitch
   // from interpolated rising zero crossings.
   auto amp = [&](const std::vector<float>& x, double hz) {
      double re = 0.0, im = 0.0, wsum = 0.0;
      const size_t n = x.size();
      for (size_t i = 0; i < n; i++)
      {
         const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)(n - 1));
         const double ph = 2.0 * M_PI * hz * (double)i / sr;
         re += w * x[i] * std::cos(ph);
         im += w * x[i] * std::sin(ph);
         wsum += w;
      }
      return 2.0 * std::sqrt(re * re + im * im) / wsum;
   };
   auto pitchHz = [&](const std::vector<float>& x) {
      double first = -1.0, last = -1.0;
      int crossings = 0;
      for (size_t i = 1; i < x.size(); i++)
         if (x[i - 1] < 0.0f && x[i] >= 0.0f)
         {
            const double t = (double)(i - 1) + (double)(-x[i - 1] / (x[i] - x[i - 1]));
            if (first < 0.0)
               first = t;
            else
               crossings++;
            last = t;
         }
      return crossings > 0 ? sr * crossings / (last - first) : 0.0;
   };
   auto tail = [](const std::vector<float>& x, size_t from) { return std::vector<float>(x.begin() + (long)from, x.end()); };

   // 1. A lone carrier (algorithm "4, 3, 2, 1", only operator 1 up).
   {
      Rig r;
      make(r, [](FMNode& n) {
         n.algorithm = 10;
         for (int o = 1; o < FMDsp::kNumOps; o++)
            n.ops[o].level = 0.0f;
      });
      note(r, 69, true);
      const std::vector<float> x = tail(render(r, (int)sr), 4800);
      const double hz = pitchHz(x);
      const double cents = 1200.0 * std::log2(hz / 440.0);
      const double a1 = amp(x, 440.0);
      double worst = 0.0;
      for (int h = 2; h <= 8; h++)
         worst = std::max(worst, amp(x, 440.0 * h));
      const double puritydB = 20.0 * std::log10(worst / a1 + 1e-12);
      const bool good = std::fabs(cents) < 0.5 && puritydB < -90.0;
      printf("FMTEST carrier: %.3f Hz (%+.3f c), harmonics %.1f dB  %s\n", hz, cents, puritydB, good ? "OK" : "FAIL");
      ok = ok && good;
   }

   // 2. Operator 2 at 3x into operator 1: the first sidebands sit at 1760 and
   // 880 Hz at J1(b)/J0(b) of the carrier, b = 2 pi * level.
   {
      const float level = 0.2f;
      Rig r;
      make(r, [&](FMNode& n) {
         n.algorithm = 0;
         n.ops[1].ratio = 3.0f;
         n.ops[1].level = level;
         n.ops[2].level = 0.0f;
         n.ops[3].level = 0.0f;
      });
      note(r, 69, true);
      const std::vector<float> x = tail(render(r, (int)sr), 4800);
      const double b = 2.0 * M_PI * level;
      auto besselJ = [](int n, double x) { // power series; libc++ has no cyl_bessel_j
         double sum = 0.0, term = std::pow(x / 2.0, n) / std::tgamma(n + 1.0);
         for (int k = 0; k < 30; k++)
         {
            sum += term;
            term *= -(x * x / 4.0) / ((k + 1.0) * (k + 1.0 + n));
         }
         return sum;
      };
      const double want = 20.0 * std::log10(besselJ(1, b) / besselJ(0, b));
      const double up = 20.0 * std::log10(amp(x, 1760.0) / amp(x, 440.0));
      const double down = 20.0 * std::log10(amp(x, 880.0) / amp(x, 440.0));
      const bool good = std::fabs(up - want) < 0.3 && std::fabs(down - want) < 0.3;
      printf("FMTEST sidebands: 1760 Hz %.2f dB, 880 Hz %.2f dB (Bessel %.2f)  %s\n", up, down, want,
             good ? "OK" : "FAIL");
      ok = ok && good;
   }

   // 3. A fixed carrier plays its own frequency whatever the note.
   {
      Rig r;
      make(r, [](FMNode& n) {
         n.algorithm = 10;
         n.ops[0].fixed = true;
         n.ops[0].freq = 1000.0f;
         for (int o = 1; o < FMDsp::kNumOps; o++)
            n.ops[o].level = 0.0f;
      });
      note(r, 40, true);
      const double hz = pitchHz(tail(render(r, (int)(sr / 2)), 4800));
      const bool good = std::fabs(hz - 1000.0) < 0.1;
      printf("FMTEST fixed: note 40 plays %.3f Hz  %s\n", hz, good ? "OK" : "FAIL");
      ok = ok && good;
   }

   // 4. Every algorithm at the default operators, with feedback: sounds, finite, bounded.
   {
      bool good = true;
      for (int a = 0; a < FMDsp::kNumAlgorithms; a++)
      {
         Rig r;
         make(r, [a](FMNode& n) {
            n.algorithm = a;
            n.feedback = 1.0f;
         });
         note(r, 60, true);
         const std::vector<float> x = render(r, (int)(sr / 4));
         float peak = 0.0f;
         bool finite = true;
         for (float v : x)
         {
            finite = finite && std::isfinite(v);
            peak = std::max(peak, std::fabs(v));
         }
         const bool fine = finite && peak > 0.05f && peak < 1.5f;
         if (!fine)
            printf("FMTEST algorithm %d (%s): peak %.3f finite %d  FAIL\n", a, FMDsp::AlgorithmAt(a).name, peak,
                   (int)finite);
         good = good && fine;
      }
      printf("FMTEST algorithms: %d sound, finite and under 1.5  %s\n", FMDsp::kNumAlgorithms, good ? "OK" : "FAIL");
      ok = ok && good;
   }

   // 5. A released note ends within its release, and frees its voice.
   {
      Rig r;
      make(r, [](FMNode&) {});
      note(r, 60, true);
      render(r, 4800);
      note(r, 60, false);
      const std::vector<float> x = render(r, 4800);
      float late = 0.0f;
      for (size_t i = 2400; i < x.size(); i++)
         late = std::max(late, std::fabs(x[i]));
      const int voices = r.node->ActiveVoices();
      const bool good = late < 1e-4f && voices == 0;
      printf("FMTEST release: peak %.2g after 50 ms of a 20 ms release, %d voices  %s\n", late, voices,
             good ? "OK" : "FAIL");
      ok = ok && good;
   }

   printf("FMTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

// INFINITE_PARTIALSTEST (v0.6 2.32): resynthesis through the real node. A
// sample played at its own pitch comes back within 1 dB per harmonic; a
// note an octave up is in tune and keeps the sample's formants (formant
// moves them on purpose); partials thins from the top; morph reaches the
// second sample's spectrum; speed and loop decide when a note ends;
// nothing is synthesised above Nyquist; the residual carries a noise
// sample's level.
bool RunPartialsFixture()
{
   const double sr = 48000.0;
   const int block = 256;
   bool ok = true;

   // 220 Hz, 12 harmonics at 0.5/h (or only the odd ones), short fades.
   auto harmonic = [&](float hz, bool oddOnly) {
      std::vector<float> x(48000, 0.0f);
      for (size_t i = 0; i < x.size(); i++)
      {
         float s = 0.0f;
         for (int h = 1; h <= 12; h++)
            if (!oddOnly || (h & 1))
               s += 0.5f / (float)h * std::sin(2.0f * (float)M_PI * hz * (float)h * (float)i / (float)sr);
         const float fade = std::min(1.0f, std::min((float)i, (float)(x.size() - 1 - i)) / 1440.0f);
         x[i] = s * fade;
      }
      return x;
   };
   const std::vector<float> src = harmonic(220.0f, false);

   PartialsNode node;
   node.LoadMono(0, src, sr);
   node.LoadMono(1, harmonic(330.0f, true), sr);
   node.WaitIdle();
   NoteEventQueue inbox;
   int frame = 0;
   AudioNode* audio = node.GetAudioNode();
   audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());

   auto play = [&](int noteNum, int frames, int offAt) {
      node.CookIfNeeded(frame++);
      audio->PrepareToPlay(sr, block);
      NoteEvent on;
      on.note = noteNum;
      on.velocity = 1.0f;
      on.isNoteOn = true;
      on.voiceId = 3;
      inbox.Push(on);
      std::vector<float> out((size_t)frames);
      std::vector<float> l(block), r(block);
      for (int done = 0; done < frames; done += block)
      {
         if (offAt >= 0 && done >= offAt && done - block < offAt)
         {
            NoteEvent off = on;
            off.isNoteOn = false;
            off.velocity = 0.0f;
            inbox.Push(off);
         }
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = block;
         audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < block && done + i < frames; i++)
            out[(size_t)(done + i)] = l[(size_t)i];
      }
      return out;
   };
   // Hann-windowed DFT amplitude of one frequency over [from, to).
   auto amp = [&](const std::vector<float>& x, double hz, size_t from, size_t to) {
      double re = 0.0, im = 0.0, wsum = 0.0;
      const size_t n = to - from;
      for (size_t i = 0; i < n; i++)
      {
         const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)(n - 1));
         const double ph = 2.0 * M_PI * hz * (double)i / sr;
         re += w * x[from + i] * std::cos(ph);
         im += w * x[from + i] * std::sin(ph);
         wsum += w;
      }
      return 2.0 * std::sqrt(re * re + im * im) / wsum;
   };
   auto db = [](double a, double b) { return 20.0 * std::log10(std::max(1e-12, a) / std::max(1e-12, b)); };
   const size_t w0 = 9600, w1 = 28800; // 0.2 .. 0.6 s, inside the sustained part

   node.pitch = node.formant = node.morph = 0.0f;
   node.partials = 48.0f;
   node.speed = 1.0f;
   node.noise = 1.0f;
   node.release = 20.0f;
   node.volume = 1.0f;
   node.loop = false;

   // 1. At its own pitch the sample comes back.
   {
      const std::vector<float> y = play(57, 33600, -1);
      double worst = 0.0;
      for (int h = 1; h <= 12; h++)
         worst = std::max(worst, std::fabs(db(amp(y, 220.0 * h, w0, w1), amp(src, 220.0 * h, w0, w1))));
      const bool good = worst <= 1.0;
      printf("PARTIALSTEST root: worst harmonic error %.3f dB over 12 (<= 1 dB), f0 %.2f Hz  %s\n", worst,
             node.AnalyzedF0(0), good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 2. An octave up, one partial: a pure 440 Hz.
   {
      node.partials = 1.0f;
      const std::vector<float> y = play(69, 33600, -1);
      double first = -1.0, last = -1.0;
      int crossings = 0;
      for (size_t i = w0 + 1; i < w1; i++)
         if (y[i - 1] < 0.0f && y[i] >= 0.0f)
         {
            const double t = (double)(i - 1) + (double)(-y[i - 1] / (y[i] - y[i - 1]));
            if (first < 0.0)
               first = t;
            else
               crossings++;
            last = t;
         }
      const double hz = crossings > 0 ? sr * crossings / (last - first) : 0.0;
      const double cents = 1200.0 * std::log2(std::max(1e-9, hz) / 440.0);
      const double second = db(amp(y, 880.0, w0, w1), amp(y, 440.0, w0, w1));
      const bool good = std::fabs(cents) < 1.0 && second < -60.0;
      printf("PARTIALSTEST pitch + partials 1: %.3f Hz (%+.3f c), 2nd harmonic %.1f dB  %s\n", hz, cents, second,
             good ? "OK" : "FAIL");
      ok = ok && good;
      node.partials = 48.0f;
   }
   // 3. Formants stay put an octave up; formant +12 moves them with it.
   {
      const std::vector<float> up = play(69, 33600, -1);
      const double held = db(amp(up, 440.0, w0, w1), amp(src, 440.0, w0, w1)); // source level at 440
      node.formant = 12.0f;
      const std::vector<float> shifted = play(57, 33600, -1);
      const double moved = db(amp(shifted, 440.0, w0, w1), amp(src, 220.0, w0, w1)); // 220's level, now at 440
      node.formant = 0.0f;
      const bool good = std::fabs(held) < 1.5 && std::fabs(moved) < 1.5;
      printf("PARTIALSTEST formants: octave up %+.2f dB from the source at 440, formant +12 %+.2f dB from 220  %s\n",
             held, moved, good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 4. Nothing above Nyquist: harmonic 9 of note 103 would alias to 19776 Hz.
   // formant follows the note here so the stack keeps its level up there.
   {
      node.formant = 46.0f;
      const std::vector<float> y = play(103, 33600, -1);
      node.formant = 0.0f;
      const double fund = amp(y, 3136.0, w0, w1);
      const double alias = db(amp(y, 48000.0 - 9.0 * 3136.0, w0, w1), fund);
      bool finite = true;
      for (float v : y)
         finite = finite && std::isfinite(v);
      const bool good = alias < -100.0 && finite && fund > 0.3;
      printf("PARTIALSTEST alias: fundamental %.3f, 19776 Hz %.1f dB below it  %s\n", fund, alias, good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 5. Speed 2 ends a held note at half the length; loop keeps it going.
   {
      node.speed = 2.0f;
      const std::vector<float> once = play(57, 72000, -1);
      node.loop = true;
      const std::vector<float> looped = play(57, 72000, -1);
      node.loop = false;
      node.speed = 1.0f;
      float lateOnce = 0.0f, lateLoop = 0.0f;
      for (size_t i = 33600; i < 72000; i++)
      {
         lateOnce = std::max(lateOnce, std::fabs(once[i]));
         lateLoop = std::max(lateLoop, std::fabs(looped[i]));
      }
      const bool good = lateOnce < 1e-4f && lateLoop > 0.1f;
      printf("PARTIALSTEST speed 2: peak after 0.7 s %.2g one-shot, %.2f looped  %s\n", lateOnce, lateLoop,
             good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 6. Morph 1 plays the odd-harmonic sample's spectrum at the note.
   {
      node.morph = 1.0f;
      const std::vector<float> y = play(57, 33600, -1);
      node.morph = 0.0f;
      const double even = db(amp(y, 440.0, w0, w1), amp(y, 660.0, w0, w1));
      const bool good = even < -30.0;
      printf("PARTIALSTEST morph 1: 440 Hz %.1f dB below 660 Hz (odd harmonics only)  %s\n", even,
             good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 7. A noise sample is carried by the residual.
   {
      std::vector<float> noiseSrc(48000);
      uint32_t rng = 0x2468ACEu;
      for (float& v : noiseSrc)
      {
         rng = rng * 1664525u + 1013904223u;
         v = 0.2f * ((float)(rng >> 8) / 8388608.0f - 1.0f);
      }
      PartialsNode noisy;
      noisy.LoadMono(0, noiseSrc, sr);
      noisy.WaitIdle();
      noisy.release = 20.0f;
      noisy.volume = 1.0f;
      NoteEventQueue nInbox;
      AudioNode* na = noisy.GetAudioNode();
      na->SetNoteInbox(&nInbox, nInbox.RegisterConsumer());
      auto rms = [&](float noiseLevel) {
         noisy.noise = noiseLevel;
         noisy.CookIfNeeded(frame++);
         na->PrepareToPlay(sr, block);
         NoteEvent on;
         on.note = 57;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         nInbox.Push(on);
         std::vector<float> l(block), r(block);
         double sum = 0.0;
         int n = 0;
         for (int done = 0; done < 28800; done += block)
         {
            float* ch[2] = { l.data(), r.data() };
            AudioBuffer buf;
            buf.channels = ch;
            buf.numChannels = 2;
            buf.numFrames = block;
            na->ProcessBlock(nullptr, 0, buf);
            if (done >= 9600)
               for (float v : l)
               {
                  sum += (double)v * v;
                  n++;
               }
         }
         return std::sqrt(sum / std::max(1, n));
      };
      double srcRms = 0.0;
      for (size_t i = 9600; i < 28800; i++)
         srcRms += (double)noiseSrc[i] * noiseSrc[i];
      srcRms = std::sqrt(srcRms / 19200.0);
      const double full = db(rms(1.0f), srcRms);
      const double none = db(rms(0.0f), srcRms);
      const bool good = std::fabs(full) < 3.0 && none < full - 1.0;
      printf("PARTIALSTEST residual: noise 1 %+.2f dB from the source, noise 0 %+.2f dB  %s\n", full, none,
             good ? "OK" : "FAIL");
      ok = ok && good;
   }

   printf("PARTIALSTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}

// INFINITE_STRINGTEST: the String synth plays in tune across the keyboard
// (the tuning allpass is solved at the fundamental), stiffness stretches the
// partials while the fundamental stays put, the bow sustains, decay and
// release set the fundamental's T60, position notches the harmonic it sits on.
bool RunStringFixture()
{
   const double sr = 48000.0;
   const int block = 256;
   bool ok = true;

   StringNode node;
   NoteEventQueue inbox;
   int frame = 0;
   AudioNode* audio = node.GetAudioNode();
   audio->SetNoteInbox(&inbox, inbox.RegisterConsumer());

   auto play = [&](int noteNum, float vel, int frames, int offAt) {
      node.CookIfNeeded(frame++);
      audio->PrepareToPlay(sr, block);
      {
         // Let the smoothed knobs land first, as they have by the time anyone plays.
         std::vector<float> l(block), r(block);
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = block;
         for (int k = 0; k < 16; k++)
            audio->ProcessBlock(nullptr, 0, buf);
      }
      NoteEvent on;
      on.note = noteNum;
      on.velocity = vel;
      on.isNoteOn = true;
      on.voiceId = 3;
      inbox.Push(on);
      std::vector<float> out((size_t)frames);
      std::vector<float> l(block), r(block);
      for (int done = 0; done < frames; done += block)
      {
         if (offAt >= 0 && done >= offAt && done - block < offAt)
         {
            NoteEvent off = on;
            off.isNoteOn = false;
            off.velocity = 0.0f;
            inbox.Push(off);
         }
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = block;
         audio->ProcessBlock(nullptr, 0, buf);
         for (int i = 0; i < block && done + i < frames; i++)
            out[(size_t)(done + i)] = l[(size_t)i];
      }
      return out;
   };
   auto amp = [&](const std::vector<float>& x, double hz, size_t from, size_t to) {
      double re = 0.0, im = 0.0, wsum = 0.0;
      const size_t n = to - from;
      for (size_t i = 0; i < n; i++)
      {
         const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * (double)i / (double)(n - 1));
         const double ph = 2.0 * M_PI * hz * (double)i / sr;
         re += w * x[from + i] * std::cos(ph);
         im += w * x[from + i] * std::sin(ph);
         wsum += w;
      }
      return 2.0 * std::sqrt(re * re + im * im) / wsum;
   };
   // The spectral peak near `hz` (within +-60 cents), to a hundredth of a cent.
   auto peak = [&](const std::vector<float>& x, double hz, size_t from, size_t to) {
      double best = hz, bestA = -1.0;
      for (int c = -60; c <= 60; c += 2)
      {
         const double f = hz * std::exp2((double)c / 1200.0);
         const double a = amp(x, f, from, to);
         if (a > bestA)
         {
            bestA = a;
            best = f;
         }
      }
      double lo = best * std::exp2(-2.0 / 1200.0), hi = best * std::exp2(2.0 / 1200.0);
      for (int it = 0; it < 40; it++)
      {
         const double m1 = lo + (hi - lo) / 3.0, m2 = hi - (hi - lo) / 3.0;
         if (amp(x, m1, from, to) < amp(x, m2, from, to))
            lo = m1;
         else
            hi = m2;
      }
      return 0.5 * (lo + hi);
   };
   auto cents = [](double f, double ref) { return 1200.0 * std::log2(f / ref); };
   auto noteHz = [](int n) { return 440.0 * std::exp2((n - 69) / 12.0); };
   auto db = [](double a, double b) { return 20.0 * std::log10(std::max(1e-12, a) / std::max(1e-12, b)); };
   auto rms = [](const std::vector<float>& x, size_t from, size_t to) {
      double s = 0.0;
      for (size_t i = from; i < to; i++)
         s += (double)x[i] * x[i];
      return std::sqrt(s / (double)std::max<size_t>(1, to - from));
   };
   auto reset = [&]() {
      node.exciter = StringNode::kPluck;
      node.position = 0.13f;
      node.tone = 0.5f;
      node.decay = 3.0f;
      node.stiffness = 0.0f;
      node.body = 0.0f;
      node.release = 150.0f;
      node.volume = 1.0f;
   };

   // 1. In tune across the keyboard, for every exciter.
   for (int ex = 0; ex < StringNode::kExciterCount; ex++)
   {
      reset();
      node.exciter = ex;
      double worst = 0.0;
      int worstNote = 0;
      for (int n = 24; n <= 108; n += 6)
      {
         const double f = noteHz(n);
         // A bowed note is measured once its stick-slip motion has settled.
         const size_t from = ex == StringNode::kBow ? 48000 : 4800;
         const size_t len = (size_t)std::max(24000.0, 60.0 * sr / f);
         const std::vector<float> y = play(n, 0.8f, (int)(from + len), -1);
         const double c = cents(peak(y, f, from, from + len), f);
         if (std::fabs(c) > std::fabs(worst))
         {
            worst = c;
            worstNote = n;
         }
      }
      const double gate = 2.0;
      const bool good = std::fabs(worst) <= gate;
      printf("STRINGTEST pitch %s: worst %+.3f c at MIDI %d over 24..108 (<= %.0f c)  %s\n",
             StringNode::ExciterNames()[(size_t)ex].c_str(), worst, worstNote, gate, good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 2. Stiffness stretches the upper partials; the fundamental stays in tune.
   {
      reset();
      node.stiffness = 1.0f;
      node.tone = 1.0f;
      double worst = 0.0, stretch = 1e9;
      for (int n : { 36, 48, 60, 72 })
      {
         const double f = noteHz(n);
         const size_t from = 4800, len = 24000;
         const std::vector<float> y = play(n, 0.8f, (int)(from + len), -1);
         worst = std::max(worst, std::fabs(cents(peak(y, f, from, from + len), f)));
         stretch = std::min(stretch, cents(peak(y, 6.0 * f, from, from + len), 6.0 * f));
      }
      const bool good = worst <= 2.0 && stretch >= 5.0;
      printf("STRINGTEST stiffness 1: fundamental worst %.3f c (<= 2), 6th partial at least %+.1f c sharp (>= +5)  %s\n",
             worst, stretch, good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 3. Decay is the fundamental's T60 while held; release is the damper's.
   {
      reset();
      node.decay = 1.0f;
      const double f = noteHz(45);
      const std::vector<float> y = play(45, 0.8f, 48000, -1);
      const double slope = db(amp(y, f, 33600, 43200), amp(y, f, 9600, 19200)) / 0.5; // dB per second
      const double t60 = -60.0 / slope;
      const bool good = std::fabs(t60 - 1.0) <= 0.1;
      printf("STRINGTEST decay 1 s: fundamental T60 %.3f s (within 10%%)  %s\n", t60, good ? "OK" : "FAIL");
      ok = ok && good;

      reset();
      node.release = 100.0f;
      const std::vector<float> z = play(45, 0.8f, 48000, 24000);
      const double drop = db(rms(z, 31200, 33600), rms(z, 21600, 24000));
      const bool good2 = drop <= -40.0;
      printf("STRINGTEST release 100 ms: %.1f dB 150 ms after note-off (<= -40)  %s\n", drop, good2 ? "OK" : "FAIL");
      ok = ok && good2;
   }
   // 4. The bow sustains while held and lets go on release.
   {
      reset();
      node.exciter = StringNode::kBow;
      const std::vector<float> y = play(50, 0.8f, 96000, 72000);
      const double early = rms(y, 19200, 38400), late = rms(y, 57600, 72000), after = rms(y, 86400, 96000);
      // Sustained: a held bow never fades (it may still be swelling in).
      const bool good = early > 0.01 && db(late, early) >= -3.0 && db(late, early) <= 6.0 && db(after, late) <= -40.0;
      printf("STRINGTEST bow: held level %+.1f dB from 0.4-0.8 s to 1.2-1.5 s (-3..+6), %.1f dB after release  %s\n",
             db(late, early), db(after, late), good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 5. Position: plucking a quarter of the way from the bridge notches the 4th harmonic.
   {
      reset();
      node.position = 0.25f;
      node.tone = 1.0f;
      const double f = noteHz(40);
      const std::vector<float> y = play(40, 0.8f, 28800, -1);
      const double h3 = amp(y, 3 * f, 4800, 28800), h4 = amp(y, 4 * f, 4800, 28800), h5 = amp(y, 5 * f, 4800, 28800);
      const double depth = db(h4, 0.5 * (h3 + h5));
      const bool good = depth <= -15.0;
      printf("STRINGTEST position 1/4: 4th harmonic %.1f dB under its neighbours (<= -15)  %s\n", depth,
             good ? "OK" : "FAIL");
      ok = ok && good;
   }
   // 6. Strike is darker than pluck at a soft hit, and the body keeps the level.
   {
      reset();
      const double f = noteHz(48);
      auto bright = [&](const std::vector<float>& y) {
         double lo = 0.0, hi = 0.0;
         for (int h = 1; h <= 3; h++)
            lo += amp(y, h * f, 2400, 12000);
         for (int h = 8; h <= 16; h++)
            hi += amp(y, h * f, 2400, 12000);
         return db(hi, lo);
      };
      const double pluck = bright(play(48, 0.4f, 12000, -1));
      node.exciter = StringNode::kStrike;
      const double strike = bright(play(48, 0.4f, 12000, -1));
      const bool good = strike <= pluck - 6.0;
      printf("STRINGTEST strike vs pluck: upper partials %.1f dB vs %.1f dB (strike >= 6 dB darker)  %s\n", strike,
             pluck, good ? "OK" : "FAIL");
      ok = ok && good;

      reset();
      const std::vector<float> dryY = play(48, 1.0f, 48000, -1);
      node.body = 1.0f;
      const std::vector<float> wetY = play(48, 1.0f, 48000, -1);
      float pk = 0.0f;
      for (float s : dryY)
         pk = std::max(pk, std::fabs(s));
      const double change = db(rms(wetY, 0, 48000), rms(dryY, 0, 48000));
      const bool good2 = std::fabs(change) <= 6.0 && pk > 0.1f && pk < 1.0f;
      printf("STRINGTEST body 1: level %+.1f dB vs dry (within 6); full-velocity pluck peak %.2f (0.1..1)  %s\n", change,
             pk, good2 ? "OK" : "FAIL");
      ok = ok && good2;
   }
   // 7. Eight bowed voices at once stay finite and bounded.
   {
      reset();
      node.exciter = StringNode::kBow;
      node.body = 1.0f;
      node.CookIfNeeded(frame++);
      audio->PrepareToPlay(sr, block);
      for (int k = 0; k < 8; k++)
      {
         NoteEvent on;
         on.note = 36 + k * 7;
         on.velocity = 1.0f;
         on.isNoteOn = true;
         on.voiceId = 10 + k;
         inbox.Push(on);
      }
      std::vector<float> l(block), r(block);
      bool finite = true;
      float pk = 0.0f;
      for (int done = 0; done < 96000; done += block)
      {
         float* ch[2] = { l.data(), r.data() };
         AudioBuffer buf;
         buf.channels = ch;
         buf.numChannels = 2;
         buf.numFrames = block;
         audio->ProcessBlock(nullptr, 0, buf);
         for (float s : l)
         {
            finite = finite && std::isfinite(s);
            pk = std::max(pk, std::fabs(s));
         }
      }
      const bool good = finite && pk < 4.0f;
      printf("STRINGTEST 8 bowed voices: finite %s, peak %.2f  %s\n", finite ? "yes" : "no", pk, good ? "OK" : "FAIL");
      ok = ok && good;
   }

   printf("STRINGTEST %s\n", ok ? "OK" : "FAIL");
   return ok;
}
}
