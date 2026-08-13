#pragma once

// A single timestamped note event, produced by a note source (Note
// Sequencer, later MIDI In) and consumed by a note-driven synth (Oscillator)
// or modulator (Envelope). See docs/plans/audio/audio-graph-semantics.md §4
// for the merge semantics this struct exists to support.
struct NoteEvent
{
   int note = 0;          // MIDI note number, 0-127
   float velocity = 0.0f; // 0..1
   bool isNoteOn = false; // false = note-off
   int frameOffset = 0;   // sample offset within the current block - this is
                          // what makes block-offset scheduling possible; see
                          // README.md P2.5's Transport::Beats().

   // Which node produced this event. Required, not decorative: merging two
   // producers into one note pin (audio-graph-semantics.md §4) means a
   // note-off must be matched to the note-on it actually closes by
   // (source, note), not by note alone, or a second source's note-off can
   // release a voice it never triggered (or worse, leave the real owner's
   // voice stuck on). Part 1 only ever wires a single producer per pin (see
   // NoteCable's single-source model, unchanged from AudioCable's), so this
   // field is inert today - but retrofitting it after multi-cable merge
   // exists would mean touching every producer and consumer, so it is here
   // from the outset per the phase brief's instruction.
   const void* source = nullptr;
};
