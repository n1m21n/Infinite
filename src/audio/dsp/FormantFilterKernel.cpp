#include "FormantFilterKernel.h"

#include "nodes/AudioEffectNode.h"

void FormantFilterKernel::PushParams(const AudioEffectNode& node, double sampleRate)
{
   mSampleRate = sampleRate;
   mMailbox.Push(kVowel, node.Param("vowel"));
   mMailbox.Push(kQ, node.Param("q"));
}

void FormantFilterKernel::ProcessBlock(const AudioBuffer& in, const AudioBuffer* /*sidechain*/, AudioBuffer& out)
{
   const int numChannels = std::min(in.numChannels, std::min(out.numChannels, kMaxChannels));

   for (int i = 0; i < out.numFrames; i++)
   {
      const float vowel = mMailbox.SmoothedValue(kVowel);
      const float q = std::max(0.5f, mMailbox.SmoothedValue(kQ));
      const FormantDsp::Formants f = FormantDsp::VowelFormants(vowel);
      constexpr float kFormantMakeup = 2.9f; // see the gain-staging note below
      const float makeup = kFormantMakeup / std::sqrt(q);

      for (int ch = 0; ch < numChannels; ch++)
      {
         mSvf[ch][0].SetSampleRate(mSampleRate);
         mSvf[ch][0].SetCutoff(f.f1, q);
         mSvf[ch][1].SetSampleRate(mSampleRate);
         mSvf[ch][1].SetCutoff(f.f2, q);
         mSvf[ch][2].SetSampleRate(mSampleRate);
         mSvf[ch][2].SetCutoff(f.f3, q);

         const float x = in.channels[ch][i];
         const float b1 = mSvf[ch][0].Process(x).band;
         const float b2 = mSvf[ch][1].Process(x).band;
         const float b3 = mSvf[ch][2].Process(x).band;

         // F1 carries the most perceptual weight, F3 the least - a flat sum
         // reads as thin/buzzy compared to a real vocal tract's energy
         // distribution across its formants.
         //
         // Gain staging: the SVF's band output peaks at q, and the old q*0.3
         // makeup on top made the formant peaks q^2*0.3 (+30 dB at the
         // default q 10, +49 dB at q 30) and broadband input +10 dB louder.
         // A band's white-noise power is pi*f/(q*fs) times its peak gain
         // squared, so kFormantMakeup/sqrt(q) holds broadband level at about
         // unity for every q - the q knob narrows the formants, it doesn't
         // turn the node up. kFormantMakeup = sqrt(48000 / (pi * 1790)):
         // 1790 Hz is the five vowels' mean weighted bandwidth sum
         // f1 + 0.49 f2 + 0.25 f3.
         out.channels[ch][i] = (b1 * 1.0f + b2 * 0.7f + b3 * 0.5f) * makeup;
      }
      for (int ch = numChannels; ch < out.numChannels; ch++)
         out.channels[ch][i] = 0.0f;
   }
}
