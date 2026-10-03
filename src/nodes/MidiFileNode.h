#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/MidiFile.h"
#include "core/NoteCable.h"

class AudioMidiFileNode;

// Turbo 0.48: MIDI File. A note source that plays a Standard MIDI File in
// sync with the transport. Positions are in beats (ticks / PPQ), so the file
// follows the app tempo, not its own; the file's tempo is shown as info and
// can be copied to the transport.
//
// Two-object rule as every note node: this INode parses on the main thread
// and hands an immutable note list to the audio half through a SampleSlotT;
// the audio half reads the transport and emits the note-ons/offs.
class MidiFileNode : public INode, public INoteSource
{
public:
   static INode* Create() { return new MidiFileNode(); }
   MidiFileNode();
   ~MidiFileNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   // Params (saved; order is the patch key order, append only).
   std::string path;
   int track = 0;            // 0 = all tracks, N = track N (1-based)
   int channel = 0;          // 0 = all, 1..16
   int transpose = 0;        // semitones, -48..48
   float velocityScale = 1.0f; // 0..2
   bool loop = true;
   int loopBars = 0;         // 0 = file length rounded up to whole bars
   int quantize = 2;         // MusicTime::RateDivision, default 1 bar
   bool play = true;

   // Main thread. Loads `p` and remembers it as `path`; false (with Status()
   // explaining) when it could not be read. Playback restarts on the grid.
   bool LoadFile(const std::string& p);
   // Reloads whatever path a patch / copy-paste restored.
   void ReloadFromPath();

   const std::string& Status() const { return mStatus; }
   const std::string& FileName() const { return mFileName; }
   bool HasFile() const { return mData != nullptr; }
   // The parsed file (main thread, UI). Null when nothing is loaded.
   const MidiFile::Data* Data() const { return mData.get(); }
   double FileTempo() const { return mData ? mData->FirstTempo() : 120.0; }
   // Track dropdown entries: "all tracks", then "N name (notes)".
   const std::vector<std::string>& TrackNames() const { return mTrackNames; }

   // Loop / play length in beats with the current params and time signature.
   double PlayLengthBeats() const;
   // Position inside the file in beats, -1 while idle or waiting for the grid.
   double PlayheadBeats() const;

private:
   void PushData();

   std::unique_ptr<AudioMidiFileNode> mAudioNode;
   std::shared_ptr<MidiFile::Data> mData;
   std::vector<std::string> mTrackNames;
   std::string mLoadedPath; // what mData came from; a different `path` reloads
   std::string mFileName;
   std::string mStatus = "no MIDI file loaded";
   int mLastCookFrame = -1;
};
