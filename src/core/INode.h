#pragma once

#include <string>
#include <vector>

// Mix-in interface for image-graph nodes, ported from BespokeSynth's IVisualNode.
// A node renders its output into a GL texture; downstream nodes pull that texture
// via GetOutputTexture(). Cooking is pull-based and memoized per frame so a node
// feeding several consumers only renders once.
class IModulator;
class IGeometrySource;
class AudioNode;
class AudioCable;
class NoteCable;

// Mix-in for a node whose output is real-time audio (owns an AudioNode - see
// the two-object rule in docs/plans/audio/README.md). Lets audio-consuming
// dispatch (IsInputSlotCompatible, the topology builder, ...) find "is this
// an audio source" generically via dynamic_cast<IAudioSource*>, the same way
// IGeometrySource already lets geometry dispatch avoid a per-node-type
// dynamic_cast chain.
class IAudioSource
{
public:
   virtual ~IAudioSource() {}
   virtual AudioNode* GetAudioNode() = 0;

   // True for a source whose signal comes from outside the process entirely
   // (Audio In: a live mic/line-in tap) rather than from its own params
   // (Wavetable, Gain's passthrough of an upstream cable). AUDIOPARAMSWEEPTEST
   // uses this to know a param can't be shown to reach the audio thread by
   // rendering a block headless - there's no hardware to capture from - the
   // same reason a note source with no note-input pin (MIDI Notes) is
   // unobservable there. Default false: the overwhelming majority of audio
   // sources generate their own signal and are perfectly testable this way.
   virtual bool IsHardwareDriven() const { return false; }

   // True if `index` (an INode::OutputCount() output index) is this node's
   // audio-buffer output. Every existing IAudioSource has exactly one output
   // and it is the audio one, hence the default of true regardless of index.
   // VideoSourceNode is the first node with both an image output and an
   // audio output on the same INode, so the audio-consuming dispatch sites
   // (IsInputSlotCompatible et al., which currently ask "is this node an
   // IAudioSource" with no notion of which pin was dragged) need this to
   // avoid treating the node's image output as an audio source too - see
   // main.cpp's `srcIsAudioNode` call sites.
   virtual bool IsAudioOutputIndex(int /*index*/) const { return true; }
};

// Mix-in for a node that produces or forwards note events (Note Sequencer;
// later MIDI In, Arpeggiator, Note Filter/Modify/Echo). Mirrors
// IAudioSource exactly, for exactly the same reason: it lets note-consuming
// dispatch (IsInputSlotCompatible, the three srcIsNoteSource call sites in
// main.cpp) find "is this a note source" generically via
// dynamic_cast<INoteSource*>, rather than a per-node-type chain that a new
// note node type could be added without updating. GetAudioNode() (not a
// GetNoteNode() of its own) is deliberate - the note-producing/consuming
// audio-thread object is still an AudioNode, just one that also overrides
// the note ports declared on AudioNode itself (NoteOutbox/SetNoteInbox);
// see docs/plans/audio/P3a-notes-prompt.md.
class INoteSource
{
public:
   virtual ~INoteSource() {}
   virtual AudioNode* GetAudioNode() = 0;
};

// Visits a node's saveable parameters.
//
// One declaration serves both directions: saving walks the fields and writes
// them, loading walks the same fields and fills them in. Writing separate save
// and load functions per node would mean two lists that have to agree, and the
// moment someone adds a parameter to one and not the other, patches quietly
// load with the wrong value.
//
// Names are stable keys in the patch file, so renaming one silently drops that
// parameter from existing patches - change the name only when the meaning
// genuinely changes.
class ParamVisitor
{
public:
   virtual ~ParamVisitor() {}
   virtual void Float(const char* name, float& value) = 0;
   virtual void Int(const char* name, int& value) = 0;
   virtual void Bool(const char* name, bool& value) = 0;
   virtual void Text(const char* name, std::string& value) = 0;
   virtual void Color(const char* name, float rgb[3]) = 0;
};

// Zero is reserved as "nothing produced yet", so the first real stamp is 1.
// Analogous to NextMeshRevision() (Mesh.h) on the geometry side; kept as an
// inline function-local static rather than a Mesh.cpp-style free function
// since INode has no matching .cpp to define one in.
inline unsigned long long NextTextureRevision()
{
   static unsigned long long sCounter = 0;
   return ++sCounter;
}

// Bumped once by each node that does real re-render work this frame (a
// cache hit does not bump it) - FilterNode's RunShaderPass, Render3DNode's
// draw passes, and so on. main.cpp compares this frame-over-frame to detect
// a fully idle patch (no node did new work) and log it, the same way a
// static Blender viewport eventually settles into "not re-rendering".
inline unsigned long long& NodeWorkCounter()
{
   static unsigned long long counter = 0;
   return counter;
}

class INode
{
public:
   virtual ~INode() {}

   virtual unsigned int GetOutputTexture() = 0;
   virtual int GetOutputWidth() const = 0;
   virtual int GetOutputHeight() const = 0;

   // Ensure this node has produced its output for the given frame (memoized).
   // Nodes with inputs should pull their inputs' CookIfNeeded() before rendering themselves.
   virtual void CookIfNeeded(int frameId) = 0;

   // Revision stamp for this node's current output texture: changes only when
   // the pixels actually change, mirroring IGeometrySource::MeshRevision() on
   // the mesh side. A node that tracks this for real lets downstream FilterNode
   // caches skip re-rendering when nothing upstream moved. The default hands
   // back a fresh value on every call - i.e. "always changed" - so any node
   // that doesn't override this stays exactly as conservative (and correct)
   // as the no-caching behavior that predates this mechanism.
   virtual unsigned long long TextureRevision() const { return NextTextureRevision(); }

   // Most nodes emit a single output. Modulators that carry more than one value
   // (an XY pad, say) report a higher count and hand back a different modulator
   // per index; see ModulatorForOutput in Modulation.h.
   virtual int OutputCount() const { return 1; }
   virtual const char* OutputLabel(int /*index*/) const { return "out"; }

   // Nodes that emit several control values return a distinct modulator per
   // output index. Returning null falls back to the node itself for output 0,
   // which is what every single-output modulator wants.
   virtual IModulator* ModulatorOutput(int /*index*/) { return nullptr; }

   // --- bypass -------------------------------------------------------------
   // A bypassed node is skipped entirely: anything pulling from it is handed
   // whatever its own pass-through input produces instead. Nodes with an image
   // input return that input's source here; sources return null, so bypassing
   // one simply removes it from the chain.
   bool bypassed = false;
   virtual INode* BypassSource() { return nullptr; }

   // Label shown next to an input pin. Defaults to A, B, C... in the editor.
   virtual const char* InputLabel(int /*slot*/) const { return nullptr; }

   // Declare every parameter that should survive a save/load round trip.
   // A node that does not override this saves its connections and position but
   // none of its settings.
   virtual void VisitParams(ParamVisitor& /*visitor*/) {}

   // Address of the pointer field backing geometry input `slot`, or nullptr if
   // this node has no geometry input there. Lets connect/disconnect/rebuild be
   // generic instead of a per-class dynamic_cast chain: a caller that wants to
   // wire slot N just does `*node->GeometryInputSlot(N) = source` instead of
   // knowing which of `input`/`pointSource`/`inputs[N]`/... to reach for.
   virtual IGeometrySource** GeometryInputSlot(int /*slot*/) { return nullptr; }

   // Same idea as GeometryInputSlot, but for modulator input pins (see MathNode).
   virtual IModulator** ModulatorInputSlot(int /*slot*/) { return nullptr; }
   virtual int ModulatorInputCount() const { return 0; }

   // Same idea as GeometryInputSlot, but for audio/note input pins. Unlike
   // GeometryInputSlot (address of a raw pointer field), these hand back the
   // AudioCable/NoteCable object itself, since AudioCable/NoteCable aren't
   // raw pointer types - matching how CableFor hands back an ImageCable*.
   virtual AudioCable* AudioInputSlot(int /*slot*/) { return nullptr; }
   virtual NoteCable* NoteInputSlot(int /*slot*/) { return nullptr; }

   // Address of this node's underlying AudioNode, for wiring note ports
   // only - distinct from IAudioSource::GetAudioNode(), which additionally
   // means "this node participates in the audio *buffer* DAG" (makes it a
   // valid cable-connectable audio source as far as IsInputSlotCompatible
   // et al are concerned). A node that owns an AudioNode purely to step
   // note-driven state on the real-time thread, but whose output is not an
   // audio buffer (Envelope: its output is a modulator value, read via
   // IModulator::Value01()), overrides this instead - it lets the topology
   // builder (main.cpp's RebuildAudioTopology) find its AudioNode to call
   // PrepareToPlay/ProcessBlock/SetNoteInbox on, without also making it
   // wireable as an audio source. Every IAudioSource already satisfies the
   // same need through its own GetAudioNode(); the topology builder checks
   // that first and only falls back to this for the note-only types that
   // need it.
   virtual AudioNode* AudioNodeForNotePorts() { return nullptr; }

   // A node that must be processed every block even when nothing downstream
   // pulls it - e.g. a Sampler that is currently capturing its audio input.
   // Without this, a node with no path to an Audio Out and no note input
   // never gets an AudioTopologyEntry, so ProcessBlock never runs and it
   // can't do work that only happens inside ProcessBlock (like recording).
   virtual bool RequiresAudioProcessing() const { return false; }

   // True for a node whose output comes from outside the process entirely -
   // a live camera, MIDI controller, or Syphon/Spout receiver - and so can't
   // be pre-synthesized for an Offline Render take (main.cpp's
   // FindHardwareDrivenNode). Distinct from IAudioSource::IsHardwareDriven
   // above (which exists for the headless audio-param sweep and predates
   // this one): this virtual lives on INode itself so non-audio hardware
   // sources (camera, MIDI, Syphon In) can answer it too, without pulling in
   // IAudioSource. A node satisfying both (Audio In) overrides this one
   // function to answer both, since the signatures match exactly.
   virtual bool IsHardwareDriven() const { return false; }

   // A param whose audible effect only shows up when another param sits at a
   // specific value (unison-spread params like "detune"/"stereoWidth" are a
   // no-op while "unison" == 1) needs that other param set first, or a sweep
   // that probes it from a fresh node's own defaults will false-FAIL it.
   // EffectDefs.h's EffectParamDef::prerequisites gives table-driven
   // AudioEffectNode instances this escape hatch already; a hand-rolled node
   // (a real C++ class, not an EffectDefs.cpp row) has no table entry for
   // FindEffectParamDef to find, so it overrides this instead. Return one
   // entry per prerequisite that must hold for `paramName` to be observable
   // in isolation.
   struct SweepParamPrereq
   {
      std::string paramName;
      float value = 0.0f;
   };
   virtual std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& /*paramName*/) const { return {}; }
};
