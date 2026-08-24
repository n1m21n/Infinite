#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "audio/AudioCaptureRing.h"
#include "audio/WavWriter.h"
#include "core/AudioCable.h"
#include "core/INode.h"

// INode types proving (P2), filling out (P2.7), and now routing (P2.8) the
// audio/note cable plumbing. Each is the main-thread half of the two-object
// rule: it owns an AudioNode-derived class (audio thread, ProcessBlock only)
// and talks to it solely through ParamMailbox/MeterRing. CookIfNeeded does no
// DSP - it just publishes this frame's param values into the audio node's
// mailbox; the real sample generation happens in AudioEngine's real-time
// callback, over the DAG topology main.cpp's RebuildAudioTopology builds
// from these nodes' AudioCable connections (see
// docs/plans/audio/audio-graph-semantics.md §3). Mixer and Splitter are the
// system's only summing/fan-out points - see that document's §1/§2.
class AudioGainNode;
class AudioMixerNode;
class AudioSplitterNode;
class AudioCaptureNode;
class AudioBlendNode;

class GainNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new GainNode(); }
   GainNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~GainNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   // Otherwise a bare unlabelled dot - see audio-node-ui-system.md §6a.
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }

   // Current output level (0..1, post-gain), for the inline meter visualizer.
   float Level() const { return mLevel; }
   void SetLevel(float v) { mLevel = v; } // written from CookIfNeeded only

   float gainDb = 0.0f;
   AudioCable input;

private:
   std::unique_ptr<AudioGainNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
};

// Rule 2 (docs/plans/audio/audio-graph-semantics.md §1): the only node that
// sums audio - every other audio input pin takes exactly one cable. 4-8
// slots, each an ordinary single-cable AudioCable (no fan-in merge at the
// pin the way note cables get - that's the whole point of forcing a node
// here instead).
class MixerNode : public INode, public IAudioSource
{
public:
   static constexpr int kSlots = 8;

   static INode* Create() { return new MixerNode(); }
   MixerNode();
   ~MixerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override
   {
      return (slot >= 0 && slot < kSlots) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kLabels[kSlots] = { "1", "2", "3", "4", "5", "6", "7", "8" };
      return (slot >= 0 && slot < kSlots) ? kLabels[slot] : nullptr;
   }

   // Current summed output level (0..1, post-gain), for the inline meter.
   float Level() const { return mLevel; }
   // Per-channel post-fader peak, for each strip's own meter. Read from the
   // cache CookIfNeeded fills - never from the audio node directly.
   float ChannelLevel(int slot) const
   {
      return (slot >= 0 && slot < kSlots) ? mChannelLevel[slot] : 0.0f;
   }

   float gainDb[kSlots] = {};
   // -1 hard left .. +1 hard right. A mixer without pan can only ever build a
   // mono sum, which is most of why the previous knob-grid version read as a
   // gain array rather than as a mixer.
   float pan[kSlots] = {};
   bool mute[kSlots] = {};
   AudioCable inputs[kSlots];

private:
   std::unique_ptr<AudioMixerNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   float mChannelLevel[kSlots] = {};
};

// A two-input crossfader: blend=0 is input A only, 1 is input B only, 0.5 is
// an equal-power centre - the routing-utility counterpart to Mixer's N-way
// sum (docs/plans/audio/audio-graph-semantics.md §1/§2 still apply: this is
// not a second place to sum two signals, it picks a point between them).
class BlendAudioNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new BlendAudioNode(); }
   BlendAudioNode();
   ~BlendAudioNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override
   {
      return slot == 0 ? &inputA : (slot == 1 ? &inputB : nullptr);
   }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "a" : (slot == 1 ? "b" : nullptr);
   }

   // Current output level (0..1, post-blend), for the inline meter.
   float Level() const { return mLevel; }

   float blend = 0.5f;
   AudioCable inputA;
   AudioCable inputB;

private:
   std::unique_ptr<AudioBlendNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
};

// Rule 1's explicit fan-out point (docs/plans/audio/audio-graph-semantics.md
// §1): an ordinary audio output pin feeds exactly one destination, so
// sending a signal to several places needs this node instead. Its own
// output is exempt from that one-cable-per-output check - up to kMaxFanout
// consumers may cable from it (see AudioSourceHasFreeOutput in main.cpp).
// On the audio thread it is a plain passthrough copy into its own pooled
// output buffer - each downstream node still reads its own buffer, so
// there's no aliasing to protect against; the node exists for graph-level
// visibility and the fan-out cap, not because copying is otherwise needed.
class SplitterNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxFanout = 4;

   static INode* Create() { return new SplitterNode(); }
   SplitterNode();
   ~SplitterNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;

   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }

   AudioCable input;

private:
   std::unique_ptr<AudioSplitterNode> mAudioNode;
   int mLastCookFrame = -1;
};

// Mirror image of AudioOutputNode: a pure source with no audio input pin of
// its own, whose audio-thread twin (AudioCaptureNode) drains the live input
// device instead of generating anything - see Platform::AudioInputCapture*
// for the tap and the lock-free ring that gets it across threads. The
// constructor/destructor register this instance's want with
// AddRef/RemoveRef; CookIfNeeded pumps the tap into existence each frame
// (main thread only, since that touches AVAudioEngine) so it self-heals if
// the output device gets stopped and restarted. ProcessBlock itself only
// ever calls the lock-free Platform::AudioInputCaptureRead.
class AudioInputNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new AudioInputNode(); }
   AudioInputNode();
   ~AudioInputNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   bool IsHardwareDriven() const override { return true; }

   // Current input level (0..1, post-trim), for the inline meter visualizer.
   float Level() const { return mLevel; }

   // Why the tap isn't live, or empty when it is. See CookIfNeeded.
   const std::string& Status() const { return mStatus; }

   float gainDb = 0.0f;

private:
   std::unique_ptr<AudioCaptureNode> mAudioNode;
   std::string mStatus;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
};

// Terminal node: no audio-thread counterpart of its own. It exists purely as
// the editor-graph terminal RebuildAudioTopology (main.cpp) walks backward
// from; it contributes nothing to the AudioTopology's node list itself.
// Whatever pooled buffer its connected source ends up writing is one of
// AudioEngine::RunTopology's terminalBufferIndices, summed into the device's
// real output buffer once every node has run - see
// docs/plans/audio/audio-graph-semantics.md §3. Two cables into one Audio Out
// is refused (Rule 2) - route through a Mixer instead.
class AudioOutputNode : public INode
{
public:
   static INode* Create() { return new AudioOutputNode(); }
   ~AudioOutputNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   // Drains the capture ring into the open WAV file, if one is open - every
   // frame, whether or not this node's body is on screen, so a scrolled-off
   // Audio Out doesn't stall its recording (the ring has ~2s of headroom, not
   // unbounded).
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }

   AudioCaptureRing& CaptureRing() { return mCaptureRing; }

   // Opens a WAV file at `path` and starts draining the capture ring into it.
   // Returns false (nothing opened) if the engine isn't running yet or the
   // file couldn't be created.
   bool StartRecording(const std::string& path);
   // Stops draining and finalizes the file's RIFF/data chunk sizes. Safe to
   // call when not recording.
   void StopRecording();
   bool IsRecording() const { return mWriter.IsOpen(); }
   double ElapsedSeconds() const { return mOpenSampleRate > 0.0 ? (double)mWriter.FramesWritten() / mOpenSampleRate : 0.0; }
   int64_t FileSizeBytes() const { return mWriter.BytesWritten(); }
   const std::string& RecordingPath() const { return mWriter.Path(); }
   uint64_t DroppedSampleCount() const { return mCaptureRing.overflowCount.load(std::memory_order_relaxed); }

   AudioCable input;

   // 0 = WAV, 1 = FLAC, 2 = MP3
   int formatIndex = 0;

   // Where "Choose..." last pointed, or empty for the ~/Desktop default -
   // not persisted as an in-progress recording (nothing about a recording
   // survives save/load, same as every other recorder in this app - see the
   // Note Capturer help text), just the destination folder for next time.
   std::string recordDirectory;

private:
   AudioCaptureRing mCaptureRing;
   AudioFileWriter mWriter;
   double mOpenSampleRate = 0.0;
};
