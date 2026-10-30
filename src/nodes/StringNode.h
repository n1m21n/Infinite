#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/INode.h"
#include "core/NoteCable.h"

class AudioStringNode;

// Physically modelled string (digital waveguide, Smith 1992; Jaffe & Smith
// 1983) on a shared modal body (Adrien 1991). Each note is a bidirectional
// string whose bridge carries the loop filter (tone), dispersion allpasses
// (stiffness) and a tuning allpass solved at the fundamental, so the note
// sounds exactly at its pitch whatever the other knobs do. The exciter
// plucks, bows (Smith's friction junction) or strikes it at `position`.
// Note-driven, 8 voices.
class StringNode : public INode, public IAudioSource
{
public:
   static constexpr int kMaxVoices = 8;

   enum Exciter
   {
      kPluck = 0,
      kBow,
      kStrike,
      kExciterCount
   };
   static const std::vector<std::string>& ExciterNames();
   // The body's mode table: box is the original small acoustic box; the
   // others are fitted from one instrument's recorded notes
   // (tools/universe/string_body_fit.py).
   enum BodyType
   {
      kBodyBox = 0,
      kBodyGuitar,
      kBodyHarp,
      kBodyViolin,
      kBodyCello,
      kBodyTypeCount
   };
   static const std::vector<std::string>& BodyTypeNames();

   static INode* Create() { return new StringNode(); }
   StringNode();
   ~StringNode() override;

   unsigned int GetOutputTexture() override { return 0; }
   int GetOutputWidth() const override { return 0; }
   int GetOutputHeight() const override { return 0; }
   void CookIfNeeded(int frameId) override;
   void VisitParams(ParamVisitor& v) override;

   AudioNode* GetAudioNode() override;

   NoteCable* NoteInputSlot(int slot) override { return slot == 0 ? &noteInput : nullptr; }
   const char* InputLabel(int slot) const override { return slot == 0 ? "notes" : nullptr; }

   std::vector<SweepParamPrereq> SweepPrerequisitesFor(const std::string& paramName) const override
   {
      // A plucked string at the default decay barely changes inside one
      // window; a short decay makes tone and stiffness show in the tail.
      if (paramName == "tone" || paramName == "stiffness")
         return { { "decay", 0.4f } };
      return {};
   }
   bool SweepSpectralSignature() const override { return true; }
   int SweepMeasureBlocks() const override { return 16; }

   int ActiveVoices() const;

   int exciter = kPluck;
   float position = 0.13f;  // 0.02..0.5, exciter's distance from the bridge
   float tone = 0.5f;       // 0..1, how bright the string stays as it rings
   float decay = 3.0f;      // 0.05..20 s, the fundamental's T60 while held
   float stiffness = 0.0f;  // 0..1, upper partials stretch sharp (piano, metal)
   float body = 0.5f;       // 0..1, dry string -> through the body's modes
   int bodyType = kBodyBox;
   float release = 150.0f;  // ms, the damper's T60 after note-off
   float hardness = 1.0f;   // 0..1, pick: soft (3 ms pulse, dull) -> hard (0.3 ms, bright)
   float pressure = 0.5f;   // 0..1, bow force: light (slips, airy) -> heavy (sticks, gritty)
   float noise = 0.0f;      // 0..1, bow hair: rosin noise in the bow's speed
   float volume = 0.8f;

   NoteCable noteInput;

private:
   std::unique_ptr<AudioStringNode> mAudioNode;
   int mLastCookFrame = -1;
};
