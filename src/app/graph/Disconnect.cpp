// Link disconnect helpers (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   void DisconnectLinkById(int id)
   {
      const LinkInfo* link = FindLink(id);
      if (link == nullptr)
         return;

      const int dstPin = link->dstPin;
      GraphNode* dst = FindNodeByIndex(GraphNode::NodeIndexFromPin(dstPin));
      if (dst == nullptr)
         return;

      if (GraphNode::IsParamPin(dstPin))
      {
         Modulation::Instance().Unbind(dst->index, GraphNode::ParamIndexFromPin(dstPin));
      }
      else if (GraphNode::IsColorPin(dstPin))
      {
         PaletteBinding::Instance().Unbind(dst->index, GraphNode::ColorIndexFromPin(dstPin));
      }
      else if (auto* render = dynamic_cast<Render3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Render3DNode::kSlots)
            render->geometry[slot] = nullptr;
         else if (slot == Render3DNode::kSlots)
            render->camera = nullptr;
         else if (slot == Render3DNode::kEnvSlot)
            render->envInput.Disconnect();
         else if (slot > Render3DNode::kSlots && slot < Render3DNode::kEnvSlot)
            render->lights[slot - Render3DNode::kSlots - 1] = nullptr;
      }
      else if (auto* geoOp = dynamic_cast<GeometryOpNode*>(dst->node.get()))
      {
         geoOp->input = nullptr;
      }
      else if (auto* inst = dynamic_cast<InstanceOnPointsNode*>(dst->node.get()))
      {
         if (GraphNode::InputSlotFromPin(dstPin) == 0)
            inst->pointSource = nullptr;
         else
            inst->instanceShape = nullptr;
      }
      else if (auto* sw3 = dynamic_cast<Switcher3DNode*>(dst->node.get()))
      {
         const int slot = GraphNode::InputSlotFromPin(dstPin);
         if (slot >= 0 && slot < Switcher3DNode::kSlots)
            sw3->inputs[slot] = nullptr;
      }
      else if (IModulator** slotField = dst->node->ModulatorInputSlot(GraphNode::InputSlotFromPin(dstPin)))
      {
         *slotField = nullptr;
      }
      else if (auto* setColor = dynamic_cast<SetColorNode*>(dst->node.get());
               setColor != nullptr && GraphNode::InputSlotFromPin(dstPin) == 2)
      {
         setColor->paletteInput = nullptr;
      }
      else if (AudioCable* audioCable = dst->node->AudioInputSlot(GraphNode::InputSlotFromPin(dstPin)))
      {
         audioCable->Disconnect();
      }
      else if (NoteCable* noteCable = dst->node->NoteInputSlot(GraphNode::InputSlotFromPin(dstPin)))
      {
         noteCable->Disconnect();
      }
      else if (ImageCable* cable = CableFor(*dst, GraphNode::InputSlotFromPin(dstPin)))
      {
         cable->Disconnect();
      }
      RebuildAudioTopology();
   }


   void DisconnectAllTo(INode* dying)
   {
      // a deleted modulator must also be cleared from any Math node feeding on it
      auto* dyingMod = dynamic_cast<IModulator*>(dying);
      auto* dyingFile = dynamic_cast<AudioFileNode*>(dying);
      auto* dyingGeometry = dynamic_cast<IGeometrySource*>(dying);
      auto* dyingXY = dynamic_cast<MacroXYNode*>(dying);
      IModulator* dyingY = dyingXY ? dyingXY->YOutput() : nullptr;
      for (GraphNode& other : gNodes)
      {
         if (auto* render = dynamic_cast<Render3DNode*>(other.node.get()))
         {
            for (int slot = 0; slot < Render3DNode::kSlots; slot++)
            {
               if (dyingGeometry != nullptr && render->geometry[slot] == dyingGeometry)
                  render->geometry[slot] = nullptr;
            }
            if ((const void*)render->camera == (const void*)dying)
               render->camera = nullptr;
            for (int i = 0; i < Render3DNode::kLightSlots; i++)
               if ((const void*)render->lights[i] == (const void*)dying)
                  render->lights[i] = nullptr;
         }
         // Every other geometry-consuming node, whatever it calls its field(s)
         // internally - GeometryInputSlot(slot) finds it generically, so a new
         // node type can't forget to register here and leave a pointer to a
         // freed node that crashes on the next cook.
         if (dyingGeometry != nullptr)
         {
            for (int slot = 0; slot < kMaxGeometrySlots; slot++)
            {
               if (IGeometrySource** field = other.node->GeometryInputSlot(slot))
                  if (*field == dyingGeometry)
                     *field = nullptr;
            }
         }
         // Audio Analyze's source used to need clearing by hand here; it is an
         // ordinary AudioCable now, so the generic audio/note teardown loop
         // below covers it like every other audio consumer.
         for (int slot = 0, modCount = other.node->ModulatorInputCount(); slot < modCount; slot++)
         {
            IModulator** slotField = other.node->ModulatorInputSlot(slot);
            if ((dyingMod != nullptr && *slotField == dyingMod) ||
                (dyingY != nullptr && *slotField == dyingY))
               *slotField = nullptr;
         }
         int inputs = InputCountFor(other);
         for (int slot = 0; slot < inputs; slot++)
         {
            ImageCable* cable = CableFor(other, slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
         // Same generic pattern as the geometry loop above, for audio/note
         // pins - AudioInputSlot()/NoteInputSlot() finds them whatever the
         // node calls its field internally, so a new audio node type can't
         // forget to register here either.
         for (int slot = 0; slot < kMaxAudioSlots; slot++)
         {
            AudioCable* cable = other.node->AudioInputSlot(slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
         for (int slot = 0; slot < kMaxNoteSlots; slot++)
         {
            NoteCable* cable = other.node->NoteInputSlot(slot);
            if (cable != nullptr && cable->GetSource() == dying)
               cable->Disconnect();
         }
      }
   }


   // Looks up the pooled-buffer index a node's AudioNode was assigned, or -1
   // if that node isn't (yet) an entry in `outOrder` - either because it has
   // no audio input connected to it at all, or (defensively) because it
   // isn't an audio source. -1 means "read as silence", same as an
   // unconnected pin - see AudioTopologyEntry in AudioEngine.h.
   AudioNode* AudioNodeOfAny(INode* node)
   {
      if (node == nullptr)
         return nullptr;
      if (auto* src = dynamic_cast<IAudioSource*>(node))
         return src->GetAudioNode();
      if (auto* noteSrc = dynamic_cast<INoteSource*>(node))
         return noteSrc->GetAudioNode();
      if (auto* spatial = dynamic_cast<SpatialMixerNode*>(node))
         return spatial->GetAudioNode(); // terminal output: renders to the device, no output pin
      if (auto* adisp = dynamic_cast<AudioDisplacementNode*>(node))
         return adisp->GetAudioNode();
      if (auto* at = dynamic_cast<AudioTextureNode*>(node))
         return at->GetAudioNode();
      if (auto* ar = dynamic_cast<AudioRibbonNode*>(node))
         return ar->GetAudioNode();
      if (auto* acr = dynamic_cast<AudioColorRampNode*>(node))
         return acr->GetAudioNode();
      return node->AudioNodeForNotePorts();
   }


   int AudioBufferIndexOf(INode* node, int pinOutputSlot, const std::unordered_map<AudioNode*, int>& bufferIndexOf)
   {
      AudioNode* an = AudioNodeOfAny(node);
      if (an == nullptr)
         return -1;
      auto it = bufferIndexOf.find(an);
      if (it == bufferIndexOf.end())
         return -1;
      int baseIdx = it->second;
      int audioSlot = 0;
      if (auto* asrc = dynamic_cast<IAudioSource*>(node))
         audioSlot = asrc->AudioOutputSlotForPin(pinOutputSlot);
      if (audioSlot < 0 || audioSlot >= an->AudioOutputCount())
         audioSlot = 0;
      return baseIdx + audioSlot;
   }


   INode* ResolvedAudioSource(INode* source, bool* didHop)
   {
      if (didHop)
         *didHop = false;
      INode* node = source;
      for (int hops = 0; node != nullptr && node->bypassed && hops < 64; hops++)
      {
         if (didHop)
            *didHop = true;
         node = node->BypassSource();
      }
      return (node != nullptr && node->bypassed) ? nullptr : node;
   }
}
