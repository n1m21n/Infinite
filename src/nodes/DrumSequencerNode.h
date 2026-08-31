#pragma once

#include <memory>
#include <string>

#include "core/AudioCable.h"
#include "core/INode.h"

class AudioDrumSequencerNode;
namespace Platform
{
   struct SampleBuffer;
}

// An 8-lane, 8-step drum machine, laid out as 8 lane cards (waveform +
// per-lane transient/decay/pitch/volume/pan) above an 8x8 step grid. Each
// step carries only trigger + velocity; every other per-lane control now
// shows on its own card rather than a single "selected lane" strip - see
// docs/plans/audio/drum-sequencer-v2-prompt.md, which supersedes the
// selected-lane-strip layout from drum-sequencer-prompt.md §1a.
//
// Free-running like Wavetable/Sampler: the pattern plays with nothing
// patched anywhere, phase-locked to Transport - step firing is derived
// every block from Transport::Beats(), never from an internal "steps fired
// so far" counter, so it survives rewind/tempo changes and a paused
// transport with no special-case code (see AudioDrumSequencerNode::
// ProcessBlock). There is no note input - this node triggers only from its
// own Transport-derived sequence.
class DrumSequencerNode : public INode, public IAudioSource
{
public:
   static constexpr int kNumLanes = 8;
   static constexpr int kMaxSteps = 8;
   static constexpr int kVoicesPerLane = 4;
   static constexpr int kWaveCache = 128;

   static INode* Create() { return new DrumSequencerNode(); }
   DrumSequencerNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~DrumSequencerNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;
   AudioNode* GetAudioNode() override;
   int OutputCount() const override { return 1 + kNumLanes; }
   const char* OutputLabel(int index) const override
   {
      static const char* kLabels[1 + kNumLanes] = {
         "out", "1", "2", "3", "4", "5", "6", "7", "8"
      };
      return (index >= 0 && index < 1 + kNumLanes) ? kLabels[index] : nullptr;
   }
   int AudioOutputSlotForPin(int pinIndex) const override { return pinIndex; }

   // Opens the native audio dialog (mirrors SamplerNode::LoadFile) and loads
   // the result into `lane` (clamped 0..kNumLanes-1). Returns false, leaving
   // whatever was loaded there untouched, on failure.
   bool LoadFileToLane(int lane, const std::string& path);

   // Re-decodes every lane whose path survived a save/load or copy/paste.
   // A lane whose file has since moved stays silent with its status set,
   // rather than blocking the rest of the patch load.
   void ReloadFromPaths();

   const std::string& FilePath(int lane) const { return laneFilePath[Clamp(lane)]; }
   const std::string& FileName(int lane) const { return laneFileName[Clamp(lane)]; }
   const std::string& LaneStatus(int lane) const { return laneStatus[Clamp(lane)]; }
   int LoadedLaneCount() const;

   // Pattern-position of the step currently sounding (or about to), derived
   // straight from Transport - see the class comment. Pure function of
   // already-published main-thread state, so this is safe to call every
   // frame with no cross-thread read.
   int CurrentStep() const;

   void Randomize();
   void ClearPattern();

   // Randomises one lane's fill + velocity (the "R" button on that row),
   // using the same seeding rules as Randomize() but scoped to a single
   // lane.
   void RandomizeLane(int lane);
   // The lane card's `x` button: clears the loaded sample (buffer, path,
   // name, status, waveform cache) but leaves the lane's pattern/knobs
   // alone.
   void ClearLane(int lane);

   // ---- pattern: trigger + velocity only, per lane -----------------------
   float stepVel[kNumLanes][kMaxSteps] = {}; // 0 = off, >0 = on at that velocity (0.05..1.0)

   // ---- per lane -----------------------------------------------------
   float laneVolume[kNumLanes];
   float lanePan[kNumLanes];
   float lanePitch[kNumLanes];
   float laneFineTune[kNumLanes]; // cents, +/-50, stacks on top of pitch - mirrors SamplerNode::finetune
   float laneDecay[kNumLanes];
   float laneTransient[kNumLanes];
   float laneStart[kNumLanes];  // 0..1, left edge of the playback range
   float laneEnd[kNumLanes];    // 0..1, right edge of the playback range
   bool laneMute[kNumLanes];
   bool laneSolo[kNumLanes];
   int laneChoke[kNumLanes]; // 0 = no choke group

   // Decimated min/max waveform for each lane card's visualizer, filled
   // once at load time (see FinishLaneBuffer) - mirrors SamplerNode's
   // waveformMin/Max/waveformCacheCount, just per-lane and smaller.
   float laneWaveMin[kNumLanes][kWaveCache] = {};
   float laneWaveMax[kNumLanes][kWaveCache] = {};
   int laneWaveCount[kNumLanes] = {};

   // ---- global ---------------------------------------------------------
   int rate = 12;      // MusicTime::RateDivision, stored as plain int (see Transport.h's mScale for why)
   int numSteps = 8;    // 1..kMaxSteps
   float swing = 0.0f;  // 0..1
   float volume = 0.8f; // node output level
   bool run = true;

   // Offsets applied on top of every lane's own value - additive for
   // transient/pitch/pan (clamped to the lane's range), additive then
   // clamped 0..1 for decay. Neutral position is 0. There is no global
   // volume offset - `volume` above (the node's output level) already
   // covers "turn everything up or down together"; a second multiplicative
   // knob doing the same thing read as a duplicate control, not a distinct
   // one, so it was removed rather than kept alongside it.
   float globalTransient = 0.0f; // -1..1
   float globalDecay = 0.0f;     // -1..1
   float globalPitch = 0.0f;     // -24..24 semitones
   float globalPan = 0.0f;       // -1..1

   // Cached each frame by DrawDrumSequencerBody: the y of the grid's top
   // row and the height of one lane row, in CANVAS space, not real screen
   // pixels. Empirically confirmed (not just inferred from a comment
   // elsewhere): a diagnostic build compared this node's own
   // ed::GetNodePosition/GetNodeSize (legitimately canvas-space) against
   // these cached values and they fall in the exact same numeric range -
   // e.g. nodePos=(900,60) nodeSize=(976,1695) with a lane card rect of
   // (1394,422,1868,694), squarely inside that box. Real screen pixels
   // would only coincide with that range by pure luck of pan/zoom; they
   // did not. So GetCursorScreenPos() read from inside this node's own
   // draw call is canvas-space here (whatever the EQDRAGTEST comment in
   // main.cpp says about ordinary widgets elsewhere - evidently something
   // about this node's wide multi-column body doesn't qualify, and no
   // static reasoning about the node-editor's internals gets to overrule
   // running the actual test). The drag release and file-drop handlers
   // therefore convert their *mouse* position to canvas space via
   // ed::ScreenToCanvas (which both already do, to find the target node in
   // the first place) and compare that against these raw cached values -
   // never the other way around.
   float gridCanvasTopY = 0.0f;
   float gridCanvasRowH = 0.0f;

   // Same idea, per lane card's full rect in canvas space (header, buttons,
   // waveform and sliders all included, not just the waveform strip),
   // cached each frame by DrawDrumLaneCard. A drag-drop release resolves
   // against these first (the cards are what the user is actually aiming
   // at) and falls back to the step-grid rect above only if the drop point
   // isn't over any card - see DrumSequencerLaneForCanvasPos. A drop over a
   // lane card used to resolve against the *grid's* rect instead, and
   // since every card sits above the grid, that always read as "before row
   // 0" and clamped to lane 0 regardless of which card was under the
   // mouse.
   float laneCardCanvasX0[kNumLanes] = {};
   float laneCardCanvasY0[kNumLanes] = {};
   float laneCardCanvasX1[kNumLanes] = {};
   float laneCardCanvasY1[kNumLanes] = {};

private:
   static int Clamp(int lane) { return lane < 0 ? 0 : (lane >= kNumLanes ? kNumLanes - 1 : lane); }

   void FinishLaneBuffer(int lane, Platform::SampleBuffer* decoded, const std::string& fileName,
                          const std::string& filePath, const std::string& status);
   void PushDirtyParams();

   std::unique_ptr<AudioDrumSequencerNode> mAudioNode;
   int mLastCookFrame = -1;

   std::string laneFilePath[kNumLanes];
   std::string laneFileName[kNumLanes];
   std::string laneStatus[kNumLanes] = { "--", "--", "--", "--", "--", "--", "--", "--" };
   // Captured at load time, main thread - the decay coefficient scales to
   // this. Reading it here rather than reaching into the audio thread's
   // SampleSlot avoids an ordering hazard: CookIfNeeded (and therefore the
   // first decay-coefficient push after a load) runs before the audio
   // thread's ProcessBlock has had a chance to adopt the just-pushed buffer,
   // so the audio-side buffer pointer would still read null.
   double laneSampleLenSec[kNumLanes] = { 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0 };

   // ---- dirty-tracking shadow copies (see PushDirtyParams) ----------------
   // CookIfNeeded runs every frame; a naive "push every lane's every param
   // every cook" is 100+ atomic stores a frame for a node this size. These
   // mirror what was last actually pushed to the audio thread, so a cook
   // with nothing changed costs a handful of memcmp/== checks and no writes
   // across the thread boundary at all.
   float mLastStepVel[kNumLanes][kMaxSteps] = {};
   float mLastLaneVolume[kNumLanes];
   float mLastLanePan[kNumLanes];
   float mLastLanePitch[kNumLanes];
   float mLastLaneFineTune[kNumLanes];
   float mLastLaneDecay[kNumLanes];
   float mLastLaneTransient[kNumLanes];
   float mLastLaneStart[kNumLanes];
   float mLastLaneEnd[kNumLanes];
   bool mLastLaneMute[kNumLanes];
   bool mLastLaneSolo[kNumLanes];
   int mLastLaneChoke[kNumLanes];
   int mLastRate = -1;
   int mLastNumSteps = -1;
   float mLastSwing = -1.0f;
   float mLastVolume = -1.0f;
   bool mLastRun = true;
   float mLastGlobalTransient = -99.0f;
   float mLastGlobalDecay = -99.0f;
   float mLastGlobalPitch = -999.0f;
   float mLastGlobalPan = -99.0f;
   bool mFirstCook = true;
   // The rate the decay/transient coefficients currently pushed were
   // computed against. PrepareToPlay may run after an earlier cook already
   // pushed coefficients using a not-yet-correct (construction-default)
   // sample rate; comparing against this each cook forces one corrective
   // re-push the moment the real rate becomes known, rather than leaving a
   // stale coefficient in place until the user happens to touch decay again.
   double mLastCoeffSampleRate = -1.0;
};
