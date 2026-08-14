#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"

class AudioSamplerNode;
namespace Platform
{
   struct SampleBuffer;
}

// A sample player: load or record a buffer, scrub/audition it by clicking
// the waveform, and play it back one-shot or looping (forward, reversed, or
// ping-pong) with independent pitch/finetune/speed/volume controls.
// Free-running (no note cable) plays continuously at MIDI note 60 the same
// way Wavetable and Oscillator sound the moment they're patched with no
// notes connected; with a note cable it becomes polyphonic over a plain
// VoiceAllocator, one voice's playback position tracked alongside it since
// VoiceAllocator itself only owns the envelope.
//
// Registered node-type name stays "Sampler" (patch files key off this, and
// NodeFactory/undo/copy-paste all use it as the lookup string) - only the
// display name changes to "sample player", the same DisplayName() override
// "Dynamics" -> "compressor" already uses for exactly this reason.
class SamplerNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new SamplerNode(); }
   SamplerNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~SamplerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   // Slot 1, not 0: slot 0 is already the note pin above, and audio/note
   // pins share one slot index space (see GraphNode.h) - a node with a note
   // pin at slot 0 and an audio pin at slot 1 has two pins, not a collision.
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &audioInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "notes" : slot == 1 ? "record in" : nullptr;
   }

   // Opens via the same native audio dialog AudioFileNode uses, decodes it,
   // and hands the decoded buffer to the audio thread. Returns false (and
   // leaves the previously loaded file, if any, untouched) on failure.
   bool LoadFile(const std::string& path);

   // Reloads from whatever path a patch/copy-paste restored. Called by
   // ReloadDerivedState after VisitParams has filled in mFilePath. A no-op
   // for a buffer that came from Record rather than a file - mFilePath is
   // empty in that case, and a recording doesn't survive save/reload today
   // (no "export recording to disk" step exists yet).
   void ReloadFromPath();

   // Starts/stops capturing whatever's connected to audioInput. Stopping
   // finalizes whatever was captured into the currently loaded buffer, the
   // same way LoadFile does for a picked file - see FinishBuffer().
   void StartRecording();
   void StopRecording();
   bool IsRecording() const { return mRecording; }

   // Auditions the loaded sample immediately from `frac` (0..1 of its
   // length), independent of any note cable - the click-the-waveform-to-
   // play interaction. A no-op with nothing loaded.
   void TriggerPreview(float frac);

   const std::string& FilePath() const { return mFilePath; }
   const std::string& FileName() const { return mFileName; }
   const std::string& Status() const { return mStatus; }

   // Decimated min/max waveform for the inline visualizer, filled once at
   // load/record time (main-thread only - the sample data itself never
   // changes during playback, so there's nothing to re-decimate per frame).
   static constexpr int kWaveformCacheSize = 256;
   float waveformMin[kWaveformCacheSize] = {};
   float waveformMax[kWaveformCacheSize] = {};
   int waveformCacheCount = 0;

   // Current playhead of the most recently triggered voice, 0..1 of the
   // loaded sample's length, drained from the audio node's MeterRing.
   float Playhead() const { return mPlayhead; }

   float pitch = 0.0f;    // semitones, +/-24, on top of the note-relative pitch
   float finetune = 0.0f; // cents, +/-50, stacks on top of pitch
   float speed = 1.0f;    // -2..2 varispeed: scales rate and pitch together
   float start = 0.0f;    // 0..1, left edge of the playback/loop range
   float end = 1.0f;      // 0..1, right edge of the playback/loop range
   float volume = 0.8f;   // 0..1
   bool loop = false;
   bool reverse = false;  // plays start<-end instead of start->end
   bool pingpong = false; // with loop on, bounces direction at each edge instead of wrapping

   NoteCable noteInput;
   AudioCable audioInput; // record source

private:
   // Shared tail of LoadFile()/StopRecording(): builds the waveform cache
   // and hands the buffer to the audio thread. Takes ownership of `decoded`.
   void FinishBuffer(Platform::SampleBuffer* decoded, const std::string& fileName,
                      const std::string& filePath, const std::string& status);

   std::unique_ptr<AudioSamplerNode> mAudioNode;
   int mLastCookFrame = -1;
   std::string mFilePath;
   std::string mFileName;
   std::string mStatus = "no sample loaded";
   float mPlayhead = 0.0f;
   bool mRecording = false;
};
