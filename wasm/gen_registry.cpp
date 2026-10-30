// The node types a Generate Samples card can place: the audio synths, note nodes, Mixer and the effect table.
#include "audio/EffectDefs.h"
#include "core/NodeFactory.h"
#include "nodes/AnalogNode.h"
#include "nodes/AudioEffectNode.h"
#include "nodes/AudioNodes.h"
#include "nodes/DrumSequencerNode.h"
#include "nodes/FMNode.h"
#include "nodes/FieldSynthNode.h"
#include "nodes/GranularNode.h"
#include "nodes/ImageSpectralSynthNode.h"
#include "nodes/MetallicNode.h"
#include "nodes/NoteNodes.h"
#include "nodes/OscillatorNode.h"
#include "nodes/PartialsNode.h"
#include "nodes/PianoNode.h"
#include "nodes/SamplerNode.h"
#include "nodes/StringNode.h"
#include "nodes/WavetableNode.h"
#include "nodes/WindNode.h"

void RegisterGenNodes()
{
   REGISTER_NODE(OscillatorNode, Oscillator, "Synths");
   REGISTER_NODE(WavetableNode, Wavetable, "Synths");
   REGISTER_NODE(AnalogNode, Analog, "Synths");
   REGISTER_NODE(FMNode, FM, "Synths");
   REGISTER_NODE(PartialsNode, Partials, "Synths");
   REGISTER_NODE(StringNode, String, "Synths");
   REGISTER_NODE(PianoNode, Piano, "Synths");
   REGISTER_NODE(WindNode, Wind, "Synths");
   REGISTER_NODE(ImageSpectralSynthNode, Spectral Synth, "Synths");
   REGISTER_NODE(MetallicNode, Metallic, "Synths");
   REGISTER_NODE(SamplerNode, Sampler, "Synths");
   REGISTER_NODE(GranularNode, Granular, "Synths");
   REGISTER_NODE(FieldSynthNode, Field Synth, "Synths");
   REGISTER_NODE(DrumSequencerNode, Drum Sequencer, "Synths");
   REGISTER_NODE(MixerNode, Mixer, "Utility");
   REGISTER_NODE(VelocityCurveNode, Velocity Curve, "Notes");
   REGISTER_NODE(HumanizerNode, Humanizer, "Notes");
   REGISTER_NODE(NoteEchoNode, Note Echo, "Notes");
   REGISTER_NODE(ArpeggiatorNode, Arpeggiator, "Notes");
   REGISTER_NODE(NoteSequencerNode, Note Sequencer, "Notes");
   REGISTER_NODE(ChorderNode, Chorder, "Notes");
   REGISTER_NODE(NoteStrumNode, Note Strum, "Notes");
   for (const EffectDef& def : GetEffectDefs())
   {
      const EffectDef* defPtr = &def;
      NodeFactory::Instance().Register(
         def.name, [defPtr]() -> INode* { return AudioEffectNode::CreateFor(*defPtr); }, def.category);
   }
}
