#pragma once

#include "core/INode.h"
#include "core/NoteCable.h"
#include "field/FieldDevice.h"
#include "field/ParamTable.h"
#include "field/SampleProgram.h"

#include <memory>
#include <string>
#include <vector>

class AudioFieldNotesNode;

// Field Notes: a note node whose body is a Field script (docs/plans/field/
// field-notes-node.md). The script runs per sample on the audio thread and
// every note(pitch, vel, len) call it makes becomes a NoteEvent on the
// note-out pin. Two-object pair: FieldNotesNode (INode, main thread) owns
// AudioFieldNotesNode (AudioNode, audio thread); they talk through
// ParamMailbox, SampleSlot and MeterRing only.
class FieldNotesNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new FieldNotesNode(); }
   FieldNotesNode();
   ~FieldNotesNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   const char* OutputLabel(int /*index*/) const override { return "notes"; }
   INode* BypassSource() override { return noteInput.GetSource(); }
   AudioNode* GetAudioNode() override;

   NodeIssue Issue() const override { return NodeIssues::FieldCompile(mLastError); }

   bool Apply();
   const std::string& LastError() const { return mLastError; }
   const std::string& Notice() const { return mNotice; }
   const Field::ParamTable& GetParamTable() const { return mParamTable; }
   Field::ParamTable& GetParamTable() { return mParamTable; }
   void SetNodeIndex(int idx) { mNodeIndex = idx; }
   int NodeIndex() const { return mNodeIndex; }

   struct Preset
   {
      const char* name;
      const char* code;
   };
   static const std::vector<Preset>& Presets();
   static const std::vector<std::string>& PresetNames();
   void LoadPreset(int index);

   Field::DeviceFile ToDeviceFile() const;
   void LoadDeviceFile(const Field::DeviceFile& device);

   // Main-thread readouts, drained from the audio thread in CookIfNeeded.
   struct RollNote
   {
      float beat = 0.0f; // transport beat the note started on
      float note = 0.0f;
      float vel = 0.0f;
   };
   static constexpr int kRollCapacity = 256;
   int RollCount() const { return mRollCount; }
   const RollNote& RollAt(int i) const { return mRoll[i]; }
   uint64_t EmittedTotal() const { return mEmittedTotal; }
   uint64_t DroppedTotal() const { return mDroppedTotal; }
   double LastBeat() const { return mLastBeat; }

   std::string code;
   int presetIndex = 3; // Euclidean
   int root = 0;        // pitch class 0..11, feeds deg()
   int scale = 0;       // MusicTime::ScaleType, feeds deg()

   NoteCable noteInput;

private:
   std::unique_ptr<AudioFieldNotesNode> mAudioNode;
   Field::ParamTable mParamTable;
   std::vector<Field::SampleParamSlot> mCompiledParams;
   Field::SampleProgram mLastCompiled;
   std::string mLastError;
   std::string mNotice;
   int mNodeIndex = -1;
   int mLastCookFrame = -1;

   RollNote mRoll[kRollCapacity];
   int mRollCount = 0;
   int mRollHead = 0;
   uint64_t mEmittedTotal = 0;
   uint64_t mDroppedTotal = 0;
   double mLastBeat = 0.0;
};
