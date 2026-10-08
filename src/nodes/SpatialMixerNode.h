#pragma once

#include <memory>

#include <string>

#include "audio/AudioCaptureRing.h"
#include "audio/AudioFileWriter.h"
#include "core/AudioCable.h"
#include "core/INode.h"

// Spatial Mixer (docs/plans/spatial/README.md, Block A): the mixer that sums in
// 3D. Each input cable is an object with azimuth / elevation / distance /
// width; the output is a binaural stereo pair for headphones. Mixer stays the
// plain L/R summing node. Rev 3: this is a TERMINAL output like Audio Out (no
// output pin, so it is not an IAudioSource): AudioTopology adds its rendered
// buffer to the device mix and feeds its own capture ring for export. Two-object rule: this INode (main thread) owns an
// AudioSpatialMixerNode (audio thread) and talks to it through atomics + a
// MeterRing only.
class AudioSpatialMixerNode;

class SpatialMixerNode : public INode
{
public:
   static constexpr int kMaxSlots = 12; // kAudioMaxNodeInputs

   static INode* Create() { return new SpatialMixerNode(); }
   SpatialMixerNode();
   ~SpatialMixerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode(); // found by AudioNodeOfAny (Disconnect.cpp); not an IAudioSource
   // Auto-growing pins: every connected pin plus one empty "+" pin, up to 12.
   AudioCable* AudioInputSlot(int slot) override
   {
      return (slot >= 0 && slot < PinCount()) ? &inputs[slot] : nullptr;
   }
   const char* InputLabel(int slot) const override;

   int PinCount() const;       // highest connected slot + 2, clamped to [1, 12]
   int ConnectedCount() const; // slots with a cable

   // Peak of the post-gain object (pre-spatialisation), from CookIfNeeded's cache.
   float ChannelLevel(int slot) const
   {
      return (slot >= 0 && slot < kMaxSlots) ? mChannelLevel[slot] : 0.0f;
   }
   float Level() const { return mLevel; }

   // ---- export (binaural WAV/FLAC, head facing front) -------------------
   AudioCaptureRing& CaptureRing() { return mCaptureRing; }
   bool StartRecording(const std::string& path);
   void StopRecording();
   bool IsRecording() const { return mWriter.IsOpen(); }
   double ElapsedSeconds() const { return mOpenSampleRate > 0.0 ? (double)mWriter.FramesWritten() / mOpenSampleRate : 0.0; }
   int64_t FileSizeBytes() const { return mWriter.BytesWritten(); }
   uint64_t DroppedSampleCount() const { return mCaptureRing.overflowCount.load(std::memory_order_relaxed); }

   int renderMode = 0;       // 0 binaural (headphones), 1 stereo (speaker-safe pan)
   int formatIndex = 0;      // 0 WAV, 1 FLAC
   bool live = false;        // not delayed to line up with other outputs (as Audio Out)
   std::string recordDirectory;

   // Per-object. azimuth: 0 front, +90 right, +-180 back (deg).
   float azimuth[kMaxSlots] = {};
   float elevation[kMaxSlots] = {};
   float distance[kMaxSlots] = {};  // metres, 0.2 .. 20
   float width[kMaxSlots] = {};     // 0..1; stereo inputs spread +-30 deg * width
   float gainDb[kMaxSlots] = {};
   bool mute[kMaxSlots] = {};
   bool solo[kMaxSlots] = {};
   int selected = 0;                // UI selection, not saved
   float outDb = 0.0f;
   AudioCable inputs[kMaxSlots];

   // Where a freshly added object starts: spread round the front arc.
   static float DefaultAzimuth(int slot);

private:
   std::unique_ptr<AudioSpatialMixerNode> mAudioNode;
   int mLastCookFrame = -1;
   float mLevel = 0.0f;
   float mChannelLevel[kMaxSlots] = {};
   AudioCaptureRing mCaptureRing;
   AudioFileWriter mWriter;
   double mOpenSampleRate = 0.0;
};
