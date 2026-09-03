#pragma once

#include "core/AudioCable.h"
#include "core/INode.h"
#include "core/NoteCable.h"
#include "field/ParamTable.h"
#include "field/SampleProgram.h"

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
   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   // Slot 1, not 0 - slot 0 is the note pin's slot in the shared pin index
   // space (see SamplerNode.h's identical convention).
   AudioCable* AudioInputSlot(int slot) override { return slot == 1 ? &audioInput : nullptr; }
   const char* InputLabel(int slot) const override
   {
      return slot == 0 ? "notes" : slot == 1 ? "in" : nullptr;
   }

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

   // Once-per-block fault counter (NaN/inf poisoning caught by the audio
   // thread's per-block sweep - see AudioFieldSampleNode::ProcessBlock),
   // drained for display the same way SamplerNode drains its meter state.
   uint64_t FaultCount() const;

   // Latest reduce.rms(in, lo, hi) publish, drained from the audio thread's
   // MeterRing (see step-08-notes.md §4: no new cross-thread channel).
   // Returns false (and leaves out untouched) if nothing has been published
   // yet or the kernel has no reduce.rms statement.
   bool ReadRmsLatest(float& out);

   std::string code = "state float y = 0\ny = y * 0.999 + in * 0.1\nout = y\n";
   int maxVoices = 8;

   NoteCable noteInput;
   AudioCable audioInput;

private:
   std::unique_ptr<AudioFieldSampleNode> mAudioNode;
   Field::ParamTable mParamTable;
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
