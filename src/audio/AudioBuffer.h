#pragma once

// Non-owning view over a block of planar audio. channels[ch][0..numFrames)
// is contiguous per channel, matching the layout AVAudioSourceNode's render
// block actually hands us (see Platform.mm) - not interleaved.
struct AudioBuffer
{
   float** channels = nullptr;
   int numChannels = 0;
   int numFrames = 0;
};
