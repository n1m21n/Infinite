#pragma once

#include "core/AudioCable.h"
#include "core/INode.h"
#include "Modulation.h"
#include "field/ParamTable.h"
#include "field/PinTable.h"
#include "field/SampleProgram.h"
#include "field/FieldDevice.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class AudioFieldSampleNode;

// Field 'sample' domain node (build step 9): a Field kernel compiled to a
// register-machine program and run once per audio sample per voice, on the
// real-time audio thread. Two-object pair like every other audio node -
// this class is the main-thread INode (editor/compile), AudioFieldSampleNode
// (defined in the .cpp) is the audio-thread AudioNode (DSP). The compiler
// (BackendRegister.cpp) is never reachable from AudioFieldSampleNode.
class FieldSampleNode : public INode, public IAudioSource
{
public:
   static INode* Create() { return new FieldSampleNode(); }
   FieldSampleNode(); // out-of-line: mAudioNode's pointee is forward-declared here
   ~FieldSampleNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;
   // Field Effect is audio-effects-only (no note-driven presets, note input
   // removed). "in" lives at slot 0 - InputCountFor's generic audio/note pin
   // probe (main.cpp) stops counting at the first slot index that answers
   // neither AudioInputSlot nor NoteInputSlot, so leaving slot 0 unanswered
   // (as when this used to reserve it for a since-removed note pin) makes the
   // node report zero input pins and hides "in" entirely - a real functional
   // regression, not just a cosmetic one.
   AudioCable* AudioInputSlot(int slot) override { return slot == 0 ? &audioInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "in" : nullptr;
   }

   // Dynamic pins, Phase 1 (build step 11, §5.2): a second output, "rms",
   // exposes the existing reduce.rms(in, loHz, hiHz) meter reading (already
   // crossing threads via MeterRing - see ReadRmsLatest above) as a real
   // modulator output pin instead of only a read-only readout string.
   // Output 0 stays the audio buffer - IsAudioOutputIndex must be overridden
   // once OutputCount() > 1, or every downstream audio consumer breaks
   // (trap 2: the IAudioSource default is "every output index is audio").
   // Dynamic pins, Phase 2b (build step 13, §5.1): kernel `output` decls on
   // top of the native "out" and toggle "rms" outputs above, compacted with
   // no gap - see FieldElementNode's identical layering scheme and Apply()
   // for how mOutputPins gets reconciled/refused.
   int NativeOutputCount() const { return exposeRmsOutput ? 2 : 1; }
   int DeclaredOutputCount() const
   {
      int n = 0;
      for (const auto& p : mOutputPins.Pins())
         if (p.isDeclared) n++;
      return n;
   }
   int OutputCount() const override { return NativeOutputCount() + DeclaredOutputCount(); }
   const char* OutputLabel(int index) const override
   {
      if (index == 0) return "out";
      if (exposeRmsOutput && index == 1) return "rms";
      int declIdx = index - NativeOutputCount();
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx) return p.name.c_str();
         seen++;
      }
      return "out";
   }
   // A declared output pin is audio-typed only if the kernel declared it
   // `output sample audio <name>` - see the structural-type/domain table
   // (step-12 doc §5.6). Every other index (native "out", toggle "rms", and
   // any scalar/vector declared output) is not audio.
   bool IsAudioOutputIndex(int index) const override
   {
      if (index == 0) return true;
      int declIdx = index - NativeOutputCount();
      if (declIdx < 0) return false;
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx) return p.typeName == "audio";
         seen++;
      }
      return false;
   }
   IModulator* ModulatorOutput(int index) override
   {
      // Gated on exposeRmsOutput - see FieldElementNode::ModulatorOutput's
      // identical comment; index 1 does not nominally exist when the pin is
      // off (OutputCount() == 1 then), so this must not hand back a live
      // IModulator for it regardless.
      if (exposeRmsOutput && index == 1)
         return static_cast<IModulator*>(&mRmsOutput);
      int declIdx = index - NativeOutputCount();
      if (declIdx < 0)
         return nullptr;
      int seen = 0;
      for (const auto& p : mOutputPins.Pins())
      {
         if (!p.isDeclared) continue;
         if (seen == declIdx)
         {
            // Audio-typed declared outputs have no IModulator meaning
            // (same rationale as FieldElementNode's native "geo" output).
            if (p.typeName == "audio")
               return nullptr;
            // A frame-domain declared output (`bass`, ...) is always
            // `reduce.rms(...)` (enforced at reconcile time - see
            // BackendRegister.cpp), so it reads the exact same live value as
            // the fixed "rms" toggle output: no separate per-pin channel
            // needed, just hand back the existing RmsOutput instance.
            if (p.domainName == "frame")
               return static_cast<IModulator*>(&mRmsOutput);
            return (declIdx < Field::PinTable::kMaxDeclaredPins) ? &mDeclaredOutputMods[declIdx] : nullptr;
         }
         seen++;
      }
      return nullptr;
   }

   bool exposeRmsOutput = false;
   // Transient (not saved) - see FieldElementNode::pinRefusal for the
   // rationale; identical cable-orphaning refusal policy (decision 4).
   std::string pinRefusal;

   const Field::PinTable& OutputPinTable() const { return mOutputPins; }
   const Field::PinTable& InputPinTable() const { return mInputPins; }

   // Compiles `code` and, on success, hot-swaps the audio thread's program
   // via the existing SampleSlotT compile-swap channel (state transplanted
   // by (name,type) match - see BackendRegister.cpp §5.9). Leaves the
   // previously-active program in place on failure.
   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }

   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   Field::ParamTable& GetParamTable() { return mParamTable; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);
   int presetIndex = 0;

   // Field build step 17 (.infdev device files) - see FieldElementNode's
   // identical pair for the rationale.
   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   // Once-per-block fault counter (NaN/inf poisoning caught by the audio
   // thread's per-block sweep - see AudioFieldSampleNode::ProcessBlock),
   // drained for display the same way SamplerNode drains its meter state.
   uint64_t FaultCount() const;

   // Latest reduce.rms(in, lo, hi) publish, drained from the audio thread's
   // MeterRing (see step-08-notes.md §4: no new cross-thread channel).
   // Returns false (and leaves out untouched) if nothing has been published
   // yet or the kernel has no reduce.rms statement.
   bool ReadRmsLatest(float& out);

   // Recent samples of the kernel's mixed `out` signal, straight off the
   // audio thread's MeterRing - same pattern as WavetableSynthCore's
   // mScopeRing, for the Field sample editor's live waveform (item 10).
   int ReadScope(float* out, int capacity);

   std::string code = "state float y = 0\ny = y * 0.999 + in * 0.1\nout = y\n";

   AudioCable audioInput;

   // Editor-window scope cache, drained/redrawn at a capped rate - mirrors
   // WavetableNode::scopeCache (main.cpp's DrawWavetableScope).
   static constexpr int kScopeCacheCapacity = 128;
   float scopeCache[kScopeCacheCapacity] = {};
   int scopeCacheCount = 0;
   double scopeCacheTime = -1.0;

   // Modeled on MacroXYNode::YAxis (src/nodes/MacroNodes.h) - reads
   // ReadRmsLatest() through the owner rather than duplicating the
   // MeterRing drain here.
   struct RmsOutput : public IModulator
   {
      FieldSampleNode* owner = nullptr;
      float Value01() override
      {
         float v = 0.0f;
         if (owner)
            owner->ReadRmsLatest(v);
         return v;
      }
   };

private:
   std::unique_ptr<AudioFieldSampleNode> mAudioNode;
   RmsOutput mRmsOutput;
   Field::ParamTable mParamTable;

   // Dynamic pins, Phase 2b (build step 13) - see FieldElementNode's
   // identical members for the full rationale.
   Field::PinTable mOutputPins;
   Field::PinTable mInputPins;
   struct DeclaredOutputPlaceholder : public IModulator
   {
      float Value01() override { return 0.0f; }
   };
   DeclaredOutputPlaceholder mDeclaredOutputMods[Field::PinTable::kMaxDeclaredPins];
   // (name -> mailboxId) for the currently-compiled program's params, set by
   // Apply(); CookIfNeeded pushes each frame by walking this list rather
   // than assuming ParamTable's own iteration order matches mailbox id
   // (ParamTable::Reconcile is free to reorder/append relative to the
   // compiled program's declaration order).
   std::vector<Field::SampleParamSlot> mCompiledParams;
   // Main thread's own retained copy of the last successfully compiled
   // program, used only as the `previous` argument for (name,type) state
   // transplant resolution on the *next* Apply() - the audio thread's copy
   // is a separate object (ownership transferred via PushProgram), so this
   // one exists purely so Apply() never has to read the live audio-thread
   // program back across threads.
   Field::SampleProgram mLastCompiled;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   int mLastCookFrame = -1;
};
