#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

// Turbo 0.57: Note Looper, a MIDI pattern recorder in the spirit of the MPC
// sequencer. REC arms and recording starts on the next bar (optionally after
// a count-in bar); with a fixed length the loop closes by itself, with "free"
// it closes on the next REC (rounded up to whole bars). A closed loop plays at
// once. OVERDUB adds a layer per pass; UNDO removes the last layer, layers can
// be muted, ERASE removes the notes of the keys you hold as the playhead
// passes. Quantize (with strength and swing) is applied when playing, so the
// take keeps its real timing and the grid can change at any time. Eight
// sequences (A-H) are saved with the patch; .mid export, import and "save to
// bank" (the Pattern Player's user folder).
//
// Two-object rule: this INode keeps the sequences (main thread); the
// AudioNoteLooperNode records and plays on the audio thread. Recorded notes
// come back through a lock-free ring, the playable notes go down through a
// lock-free triple buffer, commands and params through atomics.
class AudioNoteLooperNode;

class NoteLooperNode : public INode, public INoteSource
{
public:
   static constexpr int kSeqs = 8;
   static constexpr int kMaxLayers = 8;
   static constexpr int kMaxNotesPerSeq = 4096;

   struct Note
   {
      double beat = 0.0; // start, beats from the loop start (unquantized)
      double dur = 0.25; // beats
      int key = 60;
      float vel = 0.8f;
      int id = 0;        // recording id (0 for notes loaded from a patch / file)
      int pass = -1;     // overdub pass it was recorded in (-1 = first take / loaded)
   };
   struct Layer
   {
      std::vector<Note> notes;
      bool mute = false;
      int pass = -1;
   };
   struct Sequence
   {
      double lengthBeats = 0.0; // 0 = empty
      double trimStart = 0.0;   // beats; the loop plays [trimStart, trimEnd)
      double trimEnd = 0.0;     // beats; 0 = the end of the take
      std::vector<Layer> layers;
      int NoteCount() const;
      double TrimStart() const;
      double TrimEnd() const;
      double PlayLength() const { return lengthBeats > 0.0 ? TrimEnd() - TrimStart() : 0.0; }
   };

   enum Mode { kIdle = 0, kArmed, kRecording, kPlaying };
   // How a sequence change takes over while playing: the current one plays on
   // until the next bar or its own end, or the new one starts now from its top.
   enum Switch { kSwitchBar = 0, kSwitchEnd, kSwitchNow };

   static INode* Create() { return new NoteLooperNode(); }
   NoteLooperNode();
   ~NoteLooperNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }
   AudioNode* GetAudioNode() override;
   // Plays its sequence even with nothing wired into it.
   bool RequiresAudioProcessing() const override { return true; }

   // Params.
   int lengthBars = 2;      // 0 = free, else bars for the first take
   int quantize = 4;        // index into QuantizeNames()
   float strength = 1.0f;   // 0..1
   float swing = 0.0f;      // 0..1
   bool countIn = false;
   bool thru = true;        // passes the live input through
   int activeSeq = 0;
   int switchMode = kSwitchBar;
   Sequence seqs[kSeqs];
   NoteCable noteInput;

   static const std::vector<std::string>& QuantizeNames();
   static const std::vector<std::string>& LengthNames(); // "free", "1 bar", "2 bars", ...
   static int LengthBarsFromIndex(int i);
   static int LengthIndexFromBars(int bars);
   static const std::vector<std::string>& SwitchNames();

   // Main-thread actions (the body's buttons and MIDI-learnable pins).
   void PressRec();          // arm / close a free take / toggle overdub
   void PressPlayStop();
   void ToggleOverdub();
   void Undo();              // removes the last layer of the active sequence
   void Clear();             // empties the active sequence
   void SelectSeq(int seq);  // see switchMode
   void SetTrim(double start, double end); // active sequence, beats (snapped to 1/16)
   void SetErase(bool held);
   void SetLayerMute(int layer, bool mute);

   bool ExportMidi(const std::string& path, std::string& error) const;
   bool ImportMidi(const std::string& path, std::string& error);  // into a new layer of the active sequence
   std::string SaveToBank(std::string& error) const;                // returns the written path ("" on error)

   // Readouts.
   Mode CurrentMode() const;
   bool Overdubbing() const;
   double PlayheadBeats() const;   // within the take: trim start + position in the loop (or the take while recording)
   double LoopLengthBeats() const; // the active sequence's whole take (live while a free take runs)
   bool Erasing() const { return mErase; }
   const std::string& Status() const { return mStatus; }

private:
   std::unique_ptr<AudioNoteLooperNode> mAudioNode;
   int mLastCookFrame = -1;
   int mEpoch = 1;           // bumps when the audio half must drop what it plays (switch, clear, load)
   int mIncludesUpTo = 0;    // highest recorded id already merged into the sequences
   bool mDirty = true;       // the playable snapshot must be republished
   bool mErase = false;
   double mErasePrev = -1.0;
   int mPendingSeq = -1;
   std::string mStatus;
   std::string mLoadedText[kSeqs];

   AudioNoteLooperNode& Audio();
   void DrainRecorded();
   void Publish(bool restart);
   void ApplyErase();
   static std::string Encode(const Sequence& s);
   static void Decode(const std::string& text, Sequence& s);
};
