#include "MidiFileNode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

#include "audio/AudioBuffer.h"
#include "audio/AudioNode.h"
#include "audio/MusicTime.h"
#include "audio/NoteEvent.h"
#include "audio/NoteEventQueue.h"
#include "audio/SampleSlot.h"
#include "core/Transport.h"

namespace
{
   // What the audio thread plays: every note of the file in beats, built on
   // the main thread and never modified after the hand-over.
   struct MidiPlayNote
   {
      double start = 0.0;   // beats from the file start
      double end = 0.0;
      uint16_t track = 1;   // 1-based, matches the `track` param
      uint8_t channel = 1;  // 1..16, matches the `channel` param
      uint8_t key = 60;
      uint8_t velocity = 100;
   };

   struct MidiPlayData
   {
      std::vector<MidiPlayNote> notes; // sorted by start
      double lengthBeats = 0.0;
   };

   // Next grid line at or after `beats` (a position already on the grid
   // stays where it is).
   double CeilGrid(double beats, double grid)
   {
      if (grid <= 0.0)
         return beats;
      return std::ceil(beats / grid - 1.0e-7) * grid;
   }

   double LoopBeats(double lengthBeats, int loopBars, double beatsPerBar)
   {
      if (loopBars > 0)
         return (double)loopBars * beatsPerBar;
      const double bars = std::max(1.0, std::ceil(lengthBeats / beatsPerBar - 1.0e-7));
      return bars * beatsPerBar;
   }
}

// ------------------------------------------------------------- audio half
// Timing: the file is anchored at a transport beat (`mAnchor`, a grid line
// of the start quantize). Every note-on and note-off lands at its exact
// frameOffset; the block's events are sorted with note-offs first at equal
// offsets, and a note's own off is always at least one frame after its on.
class AudioMidiFileNode : public AudioNode
{
public:
   // The audio thread no longer runs a node being destroyed, so whatever the
   // slot still holds can be freed here (main thread).
   ~AudioMidiFileNode() override
   {
      mSlot.SwapIn();
      mSlot.DrainRetired();
      delete mSlot.Active();
   }

   void PrepareToPlay(double sampleRate, int /*maxBlockSize*/) override
   {
      mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
      mNumSounding = 0;
      mWasActive = false;
      mPlayhead.store(-1.0, std::memory_order_relaxed);
   }

   void ProcessBlock(const AudioBuffer* const* /*inputs*/, int /*numInputs*/, AudioBuffer& output) override
   {
      const int numFrames = std::max(1, output.numFrames);
      mNumOut = 0;
      for (int i = 0; i < mNumSounding; i++)
         mSounding[i].onOffset = -1;

      // A new file: release the old one's notes, start again on the grid.
      if (mSlot.SwapIn())
      {
         ReleaseAll(0, numFrames);
         mNeedAnchor = true;
      }
      const MidiPlayData* data = mSlot.Active();

      Transport& transport = Transport::Instance();
      const bool active = transport.IsPlaying() && mPlay.load(std::memory_order_relaxed) && data != nullptr;
      if (!active)
      {
         ReleaseAll(0, numFrames);
         mWasActive = false;
         mPlayhead.store(-1.0, std::memory_order_relaxed);
         Flush();
         return;
      }

      // Transport (re)start, seek, rewind or loop jump: notes off, re-anchor.
      const uint32_t serial = transport.SeekSerial();
      if (!mWasActive || serial != mSeekSerial)
      {
         ReleaseAll(0, numFrames);
         mNeedAnchor = true;
      }
      mSeekSerial = serial;
      mWasActive = true;

      // Changing the track or channel filter cuts what was sounding.
      const int track = mTrack.load(std::memory_order_relaxed);
      const int channel = mChannel.load(std::memory_order_relaxed);
      if (track != mLastTrack || channel != mLastChannel)
      {
         ReleaseAll(0, numFrames);
         mLastTrack = track;
         mLastChannel = channel;
      }

      const double bpm = std::max(1.0, (double)transport.Tempo());
      const double samplesPerBeat = mSampleRate * 60.0 / bpm;
      // Turbo 0.48: AudioEngine::Process advances the transport clock before
      // running the nodes, so Beats() is this block's END (as in
      // MpcNode::Schedule / DrumSequencerNode). After a seek or loop wrap the
      // clock restarts at the block boundary, so beats0 is the new position.
      const double beats1 = transport.Beats();
      const double beats0 = beats1 - (double)numFrames / samplesPerBeat;
      const double beatsPerBar = std::max(0.25, transport.BeatsPerBar());
      const double grid = std::max(1.0 / 64.0, MusicTime::BeatsFor((MusicTime::RateDivision)std::clamp(
                                     mQuantize.load(std::memory_order_relaxed), 0, MusicTime::kNumRateDivisions - 1)));
      const bool loop = mLoop.load(std::memory_order_relaxed);
      const double playLen = loop ? LoopBeats(data->lengthBeats, mLoopBars.load(std::memory_order_relaxed), beatsPerBar)
                                  : data->lengthBeats;
      const int transpose = std::clamp(mTranspose.load(std::memory_order_relaxed), -48, 48);
      const float velScale = std::clamp(mVelocity.load(std::memory_order_relaxed), 0.0f, 2.0f);
      const size_t numNotes = data->notes.size();

      if (mNeedAnchor)
      {
         mAnchor = CeilGrid(beats0, grid);
         mNextIdx = 0;
         mDone = false;
         mNeedAnchor = false;
      }

      auto offsetOf = [&](double beat) {
         return std::clamp((int)((beat - beats0) * samplesPerBeat), 0, numFrames - 1);
      };

      for (int guard = 0; guard < 8 && !mDone && playLen > 0.0; guard++)
      {
         const double iterEnd = mAnchor + playLen;
         const double segEnd = std::min(beats1, iterEnd);

         ReleaseDue(segEnd, beats0, samplesPerBeat, numFrames);
         while (mNextIdx < numNotes)
         {
            const MidiPlayNote& pn = data->notes[mNextIdx];
            if (pn.start >= playLen - 1.0e-9)
            {
               mNextIdx = numNotes; // sorted: everything after is past the loop end
               break;
            }
            const double at = mAnchor + pn.start;
            if (at >= segEnd)
               break;
            if (mNumOut >= kMaxOnsPerBlock)
               break; // a huge chord cluster: the rest play next block
            mNextIdx++;
            if ((track != 0 && pn.track != track) || (channel != 0 && pn.channel != channel))
               continue;
            const int key = (int)pn.key + transpose;
            if (key < 0 || key > 127)
               continue;
            const float vel = std::clamp((float)pn.velocity / 127.0f * velScale, 0.0f, 1.0f);
            if (vel <= 0.0f)
               continue;
            EmitOn(key, vel, offsetOf(at), mAnchor + std::min(pn.end, playLen));
         }
         ReleaseDue(segEnd, beats0, samplesPerBeat, numFrames); // notes shorter than the block

         if (iterEnd >= beats1)
            break;
         // The file (or loop) ends inside this block.
         ReleaseAll(offsetOf(iterEnd), numFrames);
         if (loop)
         {
            mAnchor = CeilGrid(std::max(iterEnd, beats0), grid);
            mNextIdx = 0;
         }
         else
         {
            mDone = true;
         }
      }

      const double pos = beats0 - mAnchor;
      mPlayhead.store((mDone || pos < 0.0) ? -1.0 : pos, std::memory_order_relaxed);
      Flush();
   }

   NoteEventQueue* NoteOutbox() override { return &mOutbox; }

   // Main thread only.
   void PushParams(const MidiFileNode& n)
   {
      mTrack.store(std::max(0, n.track), std::memory_order_relaxed);
      mChannel.store(std::clamp(n.channel, 0, 16), std::memory_order_relaxed);
      mTranspose.store(n.transpose, std::memory_order_relaxed);
      mVelocity.store(n.velocityScale, std::memory_order_relaxed);
      mLoop.store(n.loop, std::memory_order_relaxed);
      mLoopBars.store(std::clamp(n.loopBars, 0, 256), std::memory_order_relaxed);
      mQuantize.store(n.quantize, std::memory_order_relaxed);
      mPlay.store(n.play, std::memory_order_relaxed);
   }
   void PushData(MidiPlayData* d) { mSlot.Push(d); }
   void DrainRetired() { mSlot.DrainRetired(); }
   double Playhead() const { return mPlayhead.load(std::memory_order_relaxed); }

private:
   static constexpr int kMaxSounding = 128;
   static constexpr int kMaxOut = 512;
   // NoteEventQueue holds 256: note-ons stop well short of that so the
   // note-offs of a dense block always fit.
   static constexpr int kMaxOnsPerBlock = 112;

   struct Sounding
   {
      int key;
      int voiceId;
      double endBeat;
      int onOffset; // frame of its note-on in this block, -1 = earlier block
   };

   void EmitOn(int key, float velocity, int offset, double endBeat)
   {
      if (mNumSounding >= kMaxSounding || mNumOut >= kMaxOut)
         return;
      const int id = NextVoiceId();
      NoteEvent& e = mOut[mNumOut++];
      e = NoteEvent();
      e.note = key;
      e.velocity = velocity;
      e.isNoteOn = true;
      e.frameOffset = offset;
      e.source = this;
      e.voiceId = id;
      mSounding[mNumSounding++] = { key, id, endBeat, offset };
   }

   // Emits the off of sounding[i] at `offset` (never at or before its own
   // on); returns false when that is past this block (it is then released
   // at the start of the next one).
   bool ReleaseAt(int i, int offset, int numFrames)
   {
      Sounding& s = mSounding[i];
      offset = std::max(offset, s.onOffset + 1);
      if (offset >= numFrames)
         return false;
      if (mNumOut < kMaxOut)
      {
         NoteEvent& e = mOut[mNumOut++];
         e = NoteEvent();
         e.note = s.key;
         e.velocity = 0.0f;
         e.isNoteOn = false;
         e.frameOffset = offset;
         e.source = this;
         e.voiceId = s.voiceId;
      }
      mSounding[i] = mSounding[--mNumSounding];
      return true;
   }

   void ReleaseDue(double untilBeat, double beats0, double samplesPerBeat, int numFrames)
   {
      for (int i = 0; i < mNumSounding;)
      {
         const Sounding& s = mSounding[i];
         if (s.endBeat < untilBeat)
         {
            const int offset = std::clamp((int)((s.endBeat - beats0) * samplesPerBeat), 0, numFrames - 1);
            if (ReleaseAt(i, offset, numFrames))
               continue; // slot i now holds another note
         }
         i++;
      }
   }

   void ReleaseAll(int offset, int numFrames)
   {
      for (int i = 0; i < mNumSounding;)
      {
         if (ReleaseAt(i, offset, numFrames))
            continue;
         mSounding[i].endBeat = -1.0e300; // due first thing next block
         i++;
      }
   }

   void Flush()
   {
      // Insertion sort by offset; at equal offsets note-offs go first so a
      // re-struck pitch releases before it retriggers.
      for (int i = 1; i < mNumOut; i++)
      {
         NoteEvent e = mOut[i];
         int j = i - 1;
         while (j >= 0 && (mOut[j].frameOffset > e.frameOffset ||
                           (mOut[j].frameOffset == e.frameOffset && mOut[j].isNoteOn && !e.isNoteOn)))
         {
            mOut[j + 1] = mOut[j];
            j--;
         }
         mOut[j + 1] = e;
      }
      for (int i = 0; i < mNumOut; i++)
         mOutbox.Push(mOut[i]);
      mNumOut = 0;
   }

   NoteEventQueue mOutbox;
   SampleSlotT<MidiPlayData> mSlot;
   double mSampleRate = 48000.0;

   Sounding mSounding[kMaxSounding] = {};
   int mNumSounding = 0;
   NoteEvent mOut[kMaxOut];
   int mNumOut = 0;

   bool mWasActive = false;
   bool mNeedAnchor = true;
   bool mDone = false;
   double mAnchor = 0.0;
   size_t mNextIdx = 0;
   uint32_t mSeekSerial = 0;
   int mLastTrack = 0;
   int mLastChannel = 0;

   std::atomic<int> mTrack { 0 };
   std::atomic<int> mChannel { 0 };
   std::atomic<int> mTranspose { 0 };
   std::atomic<float> mVelocity { 1.0f };
   std::atomic<bool> mLoop { true };
   std::atomic<int> mLoopBars { 0 };
   std::atomic<int> mQuantize { MusicTime::k1Bar };
   std::atomic<bool> mPlay { true };
   std::atomic<double> mPlayhead { -1.0 };
};

// ------------------------------------------------------------- main half
MidiFileNode::MidiFileNode() = default;
MidiFileNode::~MidiFileNode() = default;

AudioNode* MidiFileNode::GetAudioNode()
{
   if (!mAudioNode)
      mAudioNode = std::make_unique<AudioMidiFileNode>();
   return mAudioNode.get();
}

void MidiFileNode::CookIfNeeded(int frameId)
{
   if (frameId == mLastCookFrame)
      return;
   mLastCookFrame = frameId;
   GetAudioNode();
   // `path` set from outside (set_param, a pasted node) loads here; a failed
   // load records the path too, so it is not retried every frame.
   if (path != mLoadedPath)
      LoadFile(path);
   // Upper clamp only with a file: a patch whose file is missing keeps its track.
   track = std::max(0, track);
   if (mData)
      track = std::min(track, (int)mData->tracks.size());
   channel = std::clamp(channel, 0, 16);
   transpose = std::clamp(transpose, -48, 48);
   velocityScale = std::clamp(velocityScale, 0.0f, 2.0f);
   loopBars = std::clamp(loopBars, 0, 256);
   quantize = std::clamp(quantize, 0, MusicTime::kNumRateDivisions - 1);
   mAudioNode->PushParams(*this);
   mAudioNode->DrainRetired();
}

void MidiFileNode::VisitParams(ParamVisitor& v)
{
   v.Text("path", path);
   v.Int("track", track);
   v.Int("channel", channel);
   v.Int("transpose", transpose);
   v.Float("velocity", velocityScale);
   v.Bool("loop", loop);
   v.Int("loopBars", loopBars);
   v.Int("quantize", quantize);
   v.Bool("play", play);
}

bool MidiFileNode::LoadFile(const std::string& p)
{
   GetAudioNode();
   path = p;
   mLoadedPath = p;
   const size_t slash = p.find_last_of("/\\");
   mFileName = slash == std::string::npos ? p : p.substr(slash + 1);
   mTrackNames.clear();
   mData.reset();

   bool ok = false;
   if (p.empty())
   {
      mStatus = "no MIDI file loaded";
   }
   else
   {
      auto data = std::make_shared<MidiFile::Data>();
      std::string error;
      if (MidiFile::Load(p, *data, error))
      {
         mData = data;
         ok = true;
         char buf[160];
         snprintf(buf, sizeof(buf), "%d notes, %d tracks, %.0f bpm, %d/%d", (int)data->notes.size(),
                  (int)data->tracks.size(), data->FirstTempo(), data->timeSigNum, data->timeSigDen);
         mStatus = buf;
         if (!data->warning.empty())
            mStatus += " (" + data->warning + ")";

         int total = 0;
         for (const MidiFile::Track& t : data->tracks)
            total += t.noteCount;
         mTrackNames.push_back("all tracks (" + std::to_string(total) + ")");
         for (size_t i = 0; i < data->tracks.size(); i++)
         {
            const MidiFile::Track& t = data->tracks[i];
            std::string name = std::to_string(i + 1) + " ";
            name += t.name.empty() ? std::string("track") : t.name;
            name += " (" + std::to_string(t.noteCount) + ")";
            mTrackNames.push_back(name);
         }
      }
      else
      {
         mStatus = error;
      }
   }
   PushData();
   return ok;
}

void MidiFileNode::ReloadFromPath()
{
   if (!path.empty())
      LoadFile(path);
}

void MidiFileNode::PushData()
{
   // An empty list (failed load / cleared path) still goes through, so the
   // previous file's notes stop.
   auto* d = new MidiPlayData();
   if (mData)
   {
      d->notes.reserve(mData->notes.size());
      for (const MidiFile::Note& n : mData->notes)
      {
         MidiPlayNote pn;
         pn.start = mData->TicksToBeats(n.startTick);
         pn.end = mData->TicksToBeats(n.endTick);
         pn.track = (uint16_t)(n.track + 1);
         pn.channel = (uint8_t)(n.channel + 1);
         pn.key = n.key;
         pn.velocity = n.velocity;
         d->notes.push_back(pn);
      }
      d->lengthBeats = mData->LengthBeats();
   }
   mAudioNode->PushData(d);
}

double MidiFileNode::PlayLengthBeats() const
{
   if (!mData)
      return 0.0;
   if (!loop)
      return mData->LengthBeats();
   return LoopBeats(mData->LengthBeats(), loopBars, std::max(0.25, Transport::Instance().BeatsPerBar()));
}

double MidiFileNode::PlayheadBeats() const
{
   return mAudioNode ? mAudioNode->Playhead() : -1.0;
}
