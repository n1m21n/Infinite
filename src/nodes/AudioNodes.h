#pragma once

#include "audio/dsp/MixerMeter.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

   INode* BypassSource() override { return input.GetSource(); }
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
   static constexpr int kMaxSlots = 32;
   static constexpr int kSlots = 32;
   // The canvas body's "+" stops here; the Mixer panel adds the rest
   // (32 strips at the body's fixed width would be 25 px each).
   static constexpr int kBodySlots = 12;

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
      return (slot >= 0 && slot < numChannels) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override
   {
      static const char* kLabels[kMaxSlots] = {
         "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
         "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31", "32"
      };
      return (slot >= 0 && slot < numChannels) ? kLabels[slot] : nullptr;
   }

   // Current summed output level (0..1, post-gain), for the inline meter.
   float Level() const { return mLevel; }
   // Per-channel post-fader peak, for each strip's own meter. Read from the
   // cache CookIfNeeded fills - never from the audio node directly.
   float ChannelLevel(int slot) const
   {
      return (slot >= 0 && slot < numChannels) ? mChannelLevel[slot] : 0.0f;
   }

   // Strips past the default channel count are not in the mix until "channels"
   // reaches them, so the sweep raises it before probing their params.
   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      static const char* kStripPrefixes[] = { "gain", "pan", "mute", "solo", "trim", "phase", "solosafe" };
      for (const char* prefix : kStripPrefixes)
      {
         const size_t n = strlen(prefix);
         if (paramName.compare(0, n, prefix) == 0 && paramName.size() > n && atoi(paramName.c_str() + n) >= 8)
            return { { "channels", (float)kMaxSlots } };
      }
      return {};
   }

   // Master loudness and true peak (mixer-panel plan B4). The meter runs only while something asks:
   // the Mixer panel calls RequestMeters() each frame it draws this Mixer as a master.
   void RequestMeters() { mMeterRequested = true; }
   bool MetersRunning() const { return mMetersRunning; }
   void ResetMeters();
   float LoudnessMomentary() const { return mLoudM; }
   float LoudnessShort() const { return mLoudS; }
   float LoudnessIntegrated() const { return mLoudI; }
   float TruePeakDb() const { return mTruePeakDb; }

   int numChannels = 8;
   // Channel names, set from the Mixer panel. Empty shows the source node's title.
   std::string chName[kMaxSlots];
   // Strip colour as 0xRRGGBB; 0 follows the source node's category colour.
   unsigned int chColor[kMaxSlots] = {};
   float gainDb[kMaxSlots] = {};
   // -1 hard left .. +1 hard right. A mixer without pan can only ever build a
   // mono sum, which is most of why the previous knob-grid version read as a
   // gain array rather than as a mixer.
   float pan[kMaxSlots] = {};
   bool mute[kMaxSlots] = {};
   bool solo[kMaxSlots] = {};
   // Console strip controls (mixer-panel plan D6). Absent from older patches,
   // and every default leaves the sound exactly as it was.
   float trimDb[kMaxSlots] = {}; // pre-fader, +-24 dB
   bool phase[kMaxSlots] = {};   // polarity invert
   bool soloSafe[kMaxSlots] = {}; // stays in the mix while another strip is soloed
   // Hard-side boost relative to a unity centre: 0 = 0 dB (balance), 1 = +3 dB
   // (equal power, the law every older patch used), 2 = +4.5 dB, 3 = +6 dB (linear).
   int panLaw = 1;
   // Console master fader (mixer-panel plan §1): scales the main sum only,
   // so sends leave the strip at its own level.
   float masterDb = 0.0f;
   // Aux sends (mixer-panel plan D5): send buses 1..4 appear as extra output
   // pins "send 1".."send 4" once sendBuses reaches them, so an old patch
   // keeps its single output. kSendOffDb and below is off.
   static constexpr int kSendBuses = 4;
   static constexpr float kSendOffDb = -60.0f;
   int sendBuses = 0;
   float sendDb[kSendBuses][kMaxSlots];
   bool sendPre[kMaxSlots] = {}; // pre-fader sends for this strip (all four)
   AudioCable inputs[kMaxSlots];

   // Snapshots A-H (mixer-panel plan D8 / B6): the whole strip and master state, saved in the patch.
   // Recall crossfades on the main thread (dB and pan linear, switches at the midpoint, G3); the audio
   // node's block-rate ramp smooths each step, so a recall during playback never clicks.
   static constexpr int kSnapshots = 8;
   struct Snapshot
   {
      bool has = false;
      int count = 0;
      float master = 0.0f;
      float gain[kMaxSlots] = {}, pan[kMaxSlots] = {}, trim[kMaxSlots] = {};
      float send[kSendBuses][kMaxSlots] = {};
      bool mute[kMaxSlots] = {}, solo[kMaxSlots] = {}, phase[kMaxSlots] = {}, safe[kMaxSlots] = {}, pre[kMaxSlots] = {};
   };
   void SnapshotStore(int k);
   bool SnapshotHas(int k) const { return k >= 0 && k < kSnapshots && mSnap[k].has; }
   void SnapshotRecall(int k, float fadeMs); // fadeMs <= 0 sets the values at once
   void SnapshotAdvance(float dtMs);         // CookIfNeeded calls it with wall time; tests drive it directly
   bool SnapshotFading() const { return mFadeK >= 0; }
   int SnapshotLast() const { return mSnapLast; }
   int snapFadeMs = 200;

   int SendBuses() const { return sendBuses < 0 ? 0 : (sendBuses > kSendBuses ? kSendBuses : sendBuses); }
   int OutputCount() const override { return 1 + SendBuses(); }
   const char* OutputLabel(int index) const override
   {
      static const char* kLabels[1 + kSendBuses] = { "out", "send 1", "send 2", "send 3", "send 4" };
      return (index >= 0 && index <= SendBuses()) ? kLabels[index] : nullptr;
   }
   int AudioOutputSlotForPin(int pinIndex) const override { return pinIndex; }

private:
   std::unique_ptr<AudioMixerNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   float mChannelLevel[kMaxSlots] = {};
   bool mMeterRequested = false, mMetersRunning = false;
   float mLoudM = -120.0f, mLoudS = -120.0f, mLoudI = -120.0f, mTruePeakDb = -120.0f;
   Snapshot mSnap[kSnapshots];
   Snapshot mFadeFrom;
   int mFadeK = -1, mSnapLast = -1;
   float mFadeMs = 0.0f, mFadeAt = 0.0f;
   double mFadeClock = 0.0;
   void SnapshotApply(const Snapshot& to, float t);
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
//
// Crossover mode (v0.6 2.18, `bands` 2-4): the pins become one per band,
// low to high, split by Linkwitz-Riley 4th-order crossovers at split1..3.
// Each lower band also passes the higher splits' allpasses, so the bands
// sum to an allpass of the input: flat magnitude, no comb at the splits.
// `bands` 1 is the plain copy, bit-identical to the node before 2.18.
class SplitterNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxFanout = 4;
   static constexpr int kMaxBands = 4;

   static INode* Create() { return new SplitterNode(); }
   SplitterNode();
   ~SplitterNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      if (paramName == "split1") return { { "bands", 2.0f } };
      if (paramName == "split2") return { { "bands", 3.0f } };
      if (paramName == "split3") return { { "bands", 4.0f } };
      return {};
   }

   INode* BypassSource() override { return input.GetSource(); }
   AudioNode* GetAudioNode() override;
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &input : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "audio" : nullptr; }
   int OutputCount() const override { return Bands() > 1 ? Bands() : 1; }
   const char* OutputLabel(int index) const override
   {
      static const char* kLabels[kMaxBands] = { "1", "2", "3", "4" };
      if (Bands() <= 1)
         return index == 0 ? "out" : nullptr;
      return (index >= 0 && index < Bands()) ? kLabels[index] : nullptr;
   }
   int AudioOutputSlotForPin(int pinIndex) const override { return pinIndex; }

   int Bands() const { return bands < 1 ? 1 : (bands > kMaxBands ? kMaxBands : bands); }

   AudioCable input;
   int bands = 1;
   float split1 = 250.0f;
   float split2 = 2500.0f;
   float split3 = 8000.0f;

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
   NodeIssue Issue() const override
   {
      if (mStatus.empty())
         return {};
      return NodeIssue::Warn("No audio input: " + NodeIssues::FirstLine(mStatus) + ". The rest of the patch keeps running. Click to open audio settings.",
                             NodeIssue::Fix::AudioSettings);
   }

   float gainDb = 0.0f;
   int channelMode = 0; // 0 = Stereo (1+2), 1 = Input 1 (mono), 2 = Input 2 (mono), 3 = Input 3 (mono)...
   int deviceId = 0;    // 0 = system default, or specific device ID
   std::string deviceName;
   // Where the audio comes from: the input device, or (inside the VST3/AU plugin) the host's
   // main or sidechain bus. Inside the plugin, Default means the host's main bus and the device
   // is never opened; in the app, the host sources fall back to the device.
   enum Source { kSourceDefault = 0, kSourceHostMain = 1, kSourceHostSidechain = 2 };
   int source = kSourceDefault;

   // True when this node reads the plugin host rather than an input device.
   bool ReadsHost() const;

private:
   std::unique_ptr<AudioCaptureNode> mAudioNode;
   bool mDeviceRef = false;
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
   // `sampleRate` 0 means the engine's (a headless test has no device and passes its own).
   // bitDepth 16 or 24 (WAV, FLAC).
   bool StartRecording(const std::string& path, double sampleRate = 0.0, int bitDepth = 16);
   // Sample-exact takes (mixer-panel plan C): drop the first `skipFrames` that reach the recorder
   // (plugin latency), then keep at most `limitFrames` (0 = no limit). Set after StartRecording.
   void SetTakeWindow(int64_t skipFrames, int64_t limitFrames)
   {
      mTakeSkip = std::max<int64_t>(0, skipFrames);
      mTakeLimit = std::max<int64_t>(0, limitFrames);
   }
   int64_t TakeFrames() const { return mTakeFrames; }
   bool TakeComplete() const { return mTakeLimit > 0 && mTakeFrames >= mTakeLimit; }
   // Stops draining and finalizes the file's RIFF/data chunk sizes. Safe to
   // call when not recording.
   void StopRecording();
   bool IsRecording() const { return mWriter.IsOpen(); }
   double ElapsedSeconds() const { return mOpenSampleRate > 0.0 ? (double)mWriter.FramesWritten() / mOpenSampleRate : 0.0; }
   int64_t FileSizeBytes() const { return mWriter.BytesWritten(); }
   const std::string& RecordingPath() const { return mWriter.Path(); }
   uint64_t DroppedSampleCount() const { return mCaptureRing.overflowCount.load(std::memory_order_relaxed); }
   // Largest |sample| written to the current take, or to the last one once
   // it has stopped. The file formats clamp at full scale, so above 1.0 the
   // take was written clipped.
   float TakePeak() const { return mTakePeak; }
   // Integrated loudness (LUFS) and true peak (dBTP) of the same take; -120 when nothing was measured.
   float TakeLufs() const;
   float TakeTruePeakDb() const;

   AudioCable input;

   // 0 = WAV, 1 = FLAC, 2 = MP3
   int formatIndex = 0;

   // Live monitoring: this Audio Out is not delayed to line up with other Audio Outs that carry
   // more plugin latency. It plays as early as its own chain allows, and the other Audio Outs
   // align among themselves as before. Plugin delay compensation inside the chain is unchanged.
   bool live = false;

   // Where "Choose..." last pointed, or empty for the ~/Desktop default -
   // not persisted as an in-progress recording (nothing about a recording
   // survives save/load, same as every other recorder in this app - see the
   // Note Capturer help text), just the destination folder for next time.
   std::string recordDirectory;

private:
   AudioCaptureRing mCaptureRing;
   void MeterTake(const float* interleaved, int frames);
   AudioFileWriter mWriter;
   double mOpenSampleRate = 0.0;
   float mTakePeak = 0.0f;
   int64_t mTakeSkip = 0, mTakeLimit = 0, mTakeFrames = 0;
   std::unique_ptr<MixerMeter> mTakeMeter;
};
