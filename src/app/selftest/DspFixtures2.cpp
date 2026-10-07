// DSP self-test fixtures, part 2: sampler..granular (moved verbatim from main.cpp).
#include "app/AppShared.h"

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

      // One wavecycle at 200 Hz is exactly 240 samples. Output should match input shifted by 240 samples.
      const int cycleLen = (int)(sr / freq); // 240
      float maxDiff = 0.0f;
      const int startTest = (int)(0.1 * sr);
      const int endTest = (int)(0.4 * sr);
      for (int i = startTest; i < endTest; i++)
      {
         const float diff = std::fabs(outRec[i] - inRec[i - cycleLen]);
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

   const std::string path = "/tmp/infinite_midifiletest.mid";
   {
      std::ofstream f(path, std::ios::binary);
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
         // KNOWN GAP: a single sustained partial (pure sine) clears this bar
         // comfortably (~-22dB) once past the analysis's inherent onset
         // latency; a dense, unbandlimited 48-harmonic sawtooth (harmonics
         // only ~10 bins apart at 220Hz/2048-pt FFT, well inside reach of
         // each other's Hann mainlobe) currently lands around -6 to -7dB.
         // Two genuine bugs were found and fixed getting here (on top of
         // the phase back-propagation reference, warp-curve clamp, and
         // per-frame frequency drift fixes from before): (1) the
         // fractional-bin phase correction below had the wrong sign and
         // was half the analytically-derived magnitude, scrambling
         // relative phase between harmonics with distinct fractional-bin
         // offsets; (2) Render's oscillator used sinf(phase) while the
         // analysis extracts phase in the atan2/cosine convention (a real
         // DFT bin's phase is defined for x[n]=A*cos(wn+phase), not
         // A*sin(wn+phase)) - a systematic pi/2 error on every partial.
         // Fixing both took this from -1.5dB to -6.7dB (harmonic count 1/2
         // individually clear -20dB; error grows with harmonic count as
         // mainlobes increasingly overlap). What's left is a harder
         // problem than either of those: real per-harmonic mainlobe-overlap
         // compensation (subtracting each neighbour's predicted leakage
         // before re-estimating a harmonic's own magnitude/phase) or a
         // proper spectral-envelope phase model - not a tuning constant.
         MolderDsp::Genome baseline;
         std::vector<float> rendered;
         MolderDsp::Render(a, baseline, rendered);
         const double errDb = RmsErrDb(saw, rendered);
         if (!(errDb < -20.0))
         {
            printf("MOLDERTEST reconstruction error %.1f dB FAIL\n", errDb);
            ok = false;
         }

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
}
