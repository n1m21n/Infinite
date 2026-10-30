#include "FactoryPresets.h"

#include "INode.h"
#include "NodeFactory.h"
#include "Patch.h"

#include <map>
#include <memory>
#include <sstream>

namespace FactoryPresets
{
   namespace
   {
      struct Row { const char* type; const char* name; const char* values; };

      // "key=value key=value". Enum values are the saved ints:
      //   Analog      wave 0 saw 1 square 2 triangle 3 sine; filterType 0 off 1 ladder 2 lp12 3 lp24 8 bp12
      //   Oscillator  waveform 0 sine 1 triangle 2 saw 3 square; filterType 0 off 1 lp12 2 lp24 7 bp12
      //   Wavetable   table = index into Wavetable::TableName; warpMode 5 sync; filterType as Oscillator
      //   String      exciter 0 pluck 1 bow 2 strike; bodyType 0 box 1 guitar 2 harp 3 violin 4 cello. The body type
      //               is only the resonance: the exciter, position, decay and hardness make the instrument.
      //   Wind        instrument 0 clarinet 1 sax 2 flute 3 brass
      //   Metallic    material 0 steel 1 bell bronze 2 gong 3 cast iron; filterType 0 lp12 1 lp24 2 hp12 3 bp 4 off
      //   Delay       rateDiv = MusicTime::RateDivision (3 half, 6 quarter, 7 dotted quarter, 9 eighth,
      //               10 dotted eighth, 11 eighth triplet, 12 sixteenth)
      const Row kRows[] = {
         { "Analog", "Warm Pad", "voices=3 detune=14 spread=0.6 filterType=3 cutoff=1400 resonance=0.15 attack=450 decay=800 sustain=0.8 release=1200" },
         { "Analog", "Sub Bass", "wave1=3 wave2=1 oct1=-1 oct2=-1 oscMix=0.3 sub=0.6 detune=0 filterType=1 cutoff=500 resonance=0.1 attack=3 decay=200 sustain=0.9 release=120" },
         { "Analog", "Acid Line", "oscMix=0 detune=0 filterType=1 cutoff=450 resonance=0.8 drive=0.5 keyTrack=0.5 glide=0.06 attack=2 decay=180 sustain=0.2 release=90" },
         { "Analog", "Brass Stab", "detune=8 filterType=3 cutoff=2200 resonance=0.2 drive=0.2 attack=35 decay=250 sustain=0.6 release=180" },
         { "Analog", "Pluck", "wave1=1 pw1=0.3 filterType=2 cutoff=3000 resonance=0.3 keyTrack=0.6 attack=1 decay=160 sustain=0 release=160" },
         { "Analog", "Super Saw Lead", "voices=7 detune=32 spread=0.9 oct2=1 filterType=2 cutoff=9000 attack=4 decay=300 sustain=0.8 release=260" },
         { "Analog", "Hollow Square", "wave1=1 wave2=2 oct2=1 oscMix=0.35 filterType=8 cutoff=2600 resonance=0.25 attack=12 decay=400 sustain=0.7 release=300" },
         { "Analog", "Noise Sweep", "noise=0.7 osc1Vol=0.3 osc2Vol=0 filterType=8 cutoff=1800 resonance=0.6 attack=900 decay=1500 sustain=0.5 release=1500" },
         { "Analog", "Sync Lead", "sync=1 oct2=1 semi2=7 oscMix=0.8 filterType=3 cutoff=6000 resonance=0.2 drive=0.3 glide=0.04 attack=3 decay=300 sustain=0.7 release=200" },
         { "Analog", "FM Bell", "wave1=3 wave2=3 fm=0.55 oct2=2 oscMix=0.1 filterType=0 attack=1 decay=1400 sustain=0 release=1400" },

         { "Oscillator", "Pure Sine", "waveform=0 filterType=0 ampAttack=8 ampRelease=300" },
         { "Oscillator", "Soft Triangle Keys", "waveform=1 filterType=1 cutoff=4000 ampAttack=2 ampDecay=600 ampSustain=0.3 ampRelease=400" },
         { "Oscillator", "Fat Saw", "waveform=2 unison=5 detune=28 filterType=2 cutoff=5000 resonance=0.1" },
         { "Oscillator", "Square Bass", "waveform=3 octave=-1 filterType=2 cutoff=700 resonance=0.2 ampAttack=2 ampDecay=220 ampSustain=0.7 ampRelease=120" },
         { "Oscillator", "Sync Buzz", "waveform=2 sync=3.5 filterType=1 cutoff=7000" },
         { "Oscillator", "Slow Swell", "waveform=2 unison=3 detune=14 filterType=2 cutoff=1800 ampAttack=1200 ampDecay=800 ampSustain=0.85 ampRelease=1800" },
         { "Oscillator", "Octave Pluck", "waveform=3 octave=1 filterType=1 cutoff=3500 ampAttack=1 ampDecay=140 ampSustain=0 ampRelease=140" },
         { "Oscillator", "Thin Reed", "waveform=3 filterType=7 cutoff=1500 resonance=0.35 ampAttack=25 ampDecay=300 ampSustain=0.8 ampRelease=200" },
         { "Oscillator", "Glide Lead", "waveform=2 glide=0.12 unison=2 detune=10 filterType=2 cutoff=4200 resonance=0.25" },

         { "Wavetable", "Saw Stack", "a.table=0 a.position=0.66 a.unison=5 a.detune=22 a.stereoWidth=0.8 a.filterType=2 a.cutoff=6000" },
         { "Wavetable", "Vocal Pad", "mix=0.35 a.table=5 a.position=0.3 a.unison=3 a.ampAttack=600 a.ampRelease=1400 a.filterType=1 a.cutoff=3500 b.on=1 b.table=21 b.position=0.5 b.octave=0 b.ampAttack=800 b.ampRelease=1400" },
         { "Wavetable", "Glass Bell", "mix=0.3 a.table=11 a.position=0.4 a.ampAttack=1 a.ampDecay=1600 a.ampSustain=0 a.ampRelease=1600 b.on=1 b.table=12 b.octave=1 b.ampAttack=1 b.ampDecay=900 b.ampSustain=0 b.ampRelease=900" },
         { "Wavetable", "Reso Bass", "mix=0.4 a.table=15 a.position=0.5 a.octave=-1 a.filterType=2 a.cutoff=900 a.resonance=0.35 a.ampDecay=250 a.ampSustain=0.7 b.on=1 b.table=10 b.octave=-2" },
         { "Wavetable", "Drawbar Organ", "mix=0.3 a.table=14 a.position=0.35 a.ampAttack=6 a.ampSustain=1 a.ampRelease=80 b.on=1 b.table=14 b.position=0.8 b.octave=1 b.ampSustain=1 b.ampRelease=80" },
         { "Wavetable", "Marimba", "a.table=16 a.ampAttack=1 a.ampDecay=450 a.ampSustain=0 a.ampRelease=450" },
         { "Wavetable", "Tine Keys", "a.table=17 a.position=0.3 a.unison=2 a.detune=6 a.ampAttack=1 a.ampDecay=1200 a.ampSustain=0.15 a.ampRelease=500" },
         { "Wavetable", "Sync Sweep Lead", "a.table=13 a.position=0.6 a.warpMode=5 a.warpAmount=0.4 a.warpRatio=2 a.unison=2 a.detune=10 a.filterType=2 a.cutoff=8000" },
         // Plucked near the bridge rings bright; a harpist plucks near the middle (round); a bow sits close to the
         // bridge (sul tasto is further). Harp strings ring on undamped, so its release is long.
         { "String", "Nylon Guitar", "exciter=0 bodyType=1 body=0.85 position=0.18 hardness=0.45 tone=0.4 decay=4 stiffness=0.02 release=220" },
         { "String", "Steel Guitar", "exciter=0 bodyType=1 body=0.75 position=0.11 hardness=0.9 tone=0.7 decay=6 stiffness=0.08 release=260" },
         { "String", "Concert Harp", "exciter=0 bodyType=2 body=0.9 position=0.45 hardness=0.3 tone=0.35 decay=7 release=1500" },
         { "String", "Violin Bowed", "exciter=1 bodyType=3 body=0.9 position=0.1 pressure=0.55 noise=0.15 tone=0.6 decay=8 release=120" },
         { "String", "Violin Pizzicato", "exciter=0 bodyType=3 body=0.95 position=0.25 hardness=0.4 tone=0.4 decay=0.6 release=80" },
         { "String", "Cello Bowed", "exciter=1 bodyType=4 body=0.9 position=0.11 pressure=0.6 noise=0.12 tone=0.5 decay=10 release=220" },
         { "String", "Cello Pizzicato", "exciter=0 bodyType=4 body=0.95 position=0.22 hardness=0.35 tone=0.35 decay=1.2 release=120" },
         { "String", "Struck Dulcimer", "exciter=2 bodyType=0 body=0.7 position=0.12 hardness=0.8 stiffness=0.2 tone=0.65 decay=5 release=400" },
         // Grand: the node's defaults. Upright: more inharmonic, wider unison, less sustain. Felt: soft hammers, dark.
         { "Piano", "Grand", "hardness=0.5 tone=0.5 decay=0.5 detune=0.3 stiffness=0.5 body=0.5 release=250" },
         { "Piano", "Bright Grand", "hardness=0.8 tone=0.7 decay=0.55 detune=0.25 stiffness=0.5 body=0.65 release=250" },
         { "Piano", "Upright", "hardness=0.6 tone=0.45 decay=0.4 detune=0.5 stiffness=0.85 body=0.7 release=200" },
         { "Piano", "Felt Piano", "hardness=0.15 tone=0.25 decay=0.45 detune=0.3 stiffness=0.4 body=0.8 release=400 volume=0.9" },
         { "Piano", "Honky-Tonk", "hardness=0.7 tone=0.6 decay=0.45 detune=0.9 stiffness=0.7 body=0.6 release=220" },
         { "Piano", "Dark Grand", "hardness=0.3 tone=0.35 decay=0.6 detune=0.25 stiffness=0.5 body=0.6 release=300" },
         { "Piano", "Muted", "hardness=0.5 tone=0.4 decay=0.15 detune=0.3 stiffness=0.5 body=0.6 release=80" },
         { "Piano", "Bare Strings", "hardness=0.6 tone=0.6 decay=0.5 detune=0.35 stiffness=0.5 body=0 release=250" },
         { "Wind", "Clarinet", "instrument=0 breath=0.6 tone=0.45 noise=0.12 vibrato=0 attack=30 release=80" },
         { "Wind", "Tenor Sax", "instrument=1 breath=0.7 tone=0.6 noise=0.2 vibrato=0.25 attack=25 release=90" },
         { "Wind", "Breathy Flute", "instrument=2 breath=0.5 tone=0.55 noise=0.35 vibrato=0.3 attack=60 release=120" },
         { "Wind", "Solo Trumpet", "instrument=3 breath=0.75 tone=0.7 noise=0.08 vibrato=0.15 attack=20 release=80" },
         { "Wind", "Soft Horn", "instrument=3 breath=0.45 tone=0.3 noise=0.05 vibrato=0.1 attack=70 release=160" },
         { "Wind", "Dark Clarinet", "instrument=0 breath=0.45 tone=0.25 noise=0.08 vibrato=0 attack=40 release=100" },
         { "Wind", "Overblown Sax", "instrument=1 breath=0.92 tone=0.75 noise=0.3 vibrato=0.2 attack=15 release=80" },
         { "Wind", "Pan Flute", "instrument=2 breath=0.45 tone=0.4 noise=0.5 vibrato=0 attack=90 release=150" },
         { "Wind", "Brass Swell", "instrument=3 breath=0.65 tone=0.5 noise=0.06 vibrato=0.1 attack=400 release=300" },
         { "Wavetable", "Formant Drone", "mix=0.35 a.table=3 a.position=0.5 a.unison=4 a.detune=18 a.stereoWidth=1 a.ampAttack=1500 a.ampRelease=2500 b.on=1 b.table=9 b.octave=0 b.ampAttack=1500 b.ampRelease=2500" },

         { "Metallic", "Steel Bar", "material=0 decay=2.2 stiffness=0.25" },
         { "Metallic", "Church Bell", "material=1 octave=-1 decay=7 stiffness=0.4 transient=0.6 width=0.8" },
         { "Metallic", "Gong", "material=2 octave=-2 decay=9 stiffness=0.8 transient=0.4 width=1" },
         { "Metallic", "Anvil", "material=3 decay=0.5 stiffness=1.2 transient=1.6 drive=0.4" },
         { "Metallic", "Glass Chime", "material=1 octave=2 decay=3 stiffness=0.1 transient=1.2 filterType=2 filterCutoff=1200" },
         { "Metallic", "Muted Pan", "material=0 decay=0.35 stiffness=0.15 transient=0.8 filterType=1 filterCutoff=2500" },
         { "Metallic", "Rusty Plate", "material=2 decay=4 stiffness=1.6 drive=0.7 filterType=3 filterCutoff=1800 filterResonance=0.4" },
         { "Metallic", "Dark Tubular", "material=3 octave=-1 decay=6 stiffness=0.3 transient=0.5 filterType=0 filterCutoff=1500" },

         { "Delay", "Eighth Echo", "rateDiv=9 feedback=40 mix=0.3" },
         { "Delay", "Dotted Eighth", "rateDiv=10 feedback=45 tone=-0.2 mix=0.35" },
         { "Delay", "Ping Pong Quarter", "rateDiv=6 bounce=1 feedback=50 mix=0.35" },
         { "Delay", "Slapback", "sync=0 timeMs=110 feedback=8 mix=0.4" },
         { "Delay", "Tape Dub", "rateDiv=7 feedback=75 tone=-0.6 analog=1 saturation=0.4 mix=0.4" },
         { "Delay", "Worn Cassette", "rateDiv=9 feedback=55 tone=-0.3 analog=1 wow=0.8 flutter=0.6 saturation=0.5 mix=0.35" },
         { "Delay", "Tape Wash", "rateDiv=6 bounce=1 feedback=70 tone=-0.4 analog=1 diffusion=0.8 saturation=0.3 mix=0.4" },
         { "Delay", "Doubler", "sync=0 timeMs=28 feedback=0 pan=0.4 mix=0.5" },
         { "Delay", "Triplet Wash", "rateDiv=11 bounce=1 feedback=65 tone=-0.3 ducking=0.5 mix=0.45" },
         { "Delay", "Sixteenth Shimmer", "rateDiv=12 feedback=55 tone=0.5 mix=0.3" },
         { "Delay", "Long Half", "rateDiv=3 feedback=60 tone=-0.4 ducking=0.4 mix=0.4" },

         { "Reverb", "Small Room", "size=0.2 decay=0.5 damping=0.4 predelay=5 mix=0.2" },
         { "Reverb", "Studio Plate", "size=0.45 decay=1.6 damping=0.2 predelay=12 mix=0.25" },
         { "Reverb", "Large Hall", "size=0.8 decay=3.5 damping=0.3 predelay=30 mix=0.3" },
         { "Reverb", "Cathedral", "size=1 decay=8 damping=0.25 predelay=60 mix=0.4" },
         { "Reverb", "Dark Chamber", "size=0.5 decay=2.2 damping=0.8 predelay=15 mix=0.3" },
         { "Reverb", "Bright Ambience", "size=0.3 decay=0.9 damping=0.02 predelay=0 mix=0.2" },
         { "Reverb", "Endless Wash", "size=1 decay=20 damping=0.15 predelay=80 mix=0.6" },
         { "Reverb", "Mono Spring", "size=0.25 decay=1.4 damping=0.45 width=0 analog=1 mix=0.3" },
         { "Reverb", "Late Hall", "size=0.6 decay=2.4 damping=0.35 predelay=110 mix=0.25" },
         { "Convolution Reverb", "Wooden Room", "shape=0 size=0.2 material=0.35 distance=0.35 decay=0.6 spread=1 mix=1" },
         { "Convolution Reverb", "Concert Hall", "shape=1 size=0.8 material=0.6 distance=0.6 decay=2.4 spread=1 mix=1" },
         { "Convolution Reverb", "Stone Chamber", "shape=2 size=0.4 material=1 distance=0.5 decay=1.6 spread=1 mix=1" },
         { "Convolution Reverb", "Long Tunnel", "shape=3 size=0.9 material=0.9 distance=0.8 decay=4.5 spread=0.8 mix=1" },
         { "Convolution Reverb", "Close Booth", "shape=2 size=0.05 material=0.1 distance=0.1 decay=0.25 spread=0.6 mix=1" },
         { "Convolution Reverb", "Drum Room", "shape=0 size=0.3 material=0.7 distance=0.25 decay=0.9 spread=1 mix=0.3" },
         { "Convolution Reverb", "Soft Hall", "shape=1 size=0.65 material=0.2 distance=0.45 decay=1.8 spread=1 mix=0.35" },
         { "Convolution Reverb", "Far Cathedral", "shape=1 size=1 material=0.95 distance=1 decay=7 spread=1 mix=0.5" },

         { "Drive", "Gentle Warmth", "drive=4 tone=-0.1 mix=0.6" },
         { "Drive", "Tape Glue", "drive=8 color=0.3 tone=-0.2 output=-3" },
         { "Drive", "Tube Crunch", "drive=16 bias=0.25 color=0.5 output=-6" },
         { "Drive", "Fuzz", "drive=34 bias=0.4 color=1 tone=0.2 output=-12" },
         { "Drive", "Bright Edge", "drive=12 tone=0.7 output=-5" },
         { "Drive", "Dark Growl", "drive=24 tone=-0.8 color=0.6 output=-9" },
         { "Drive", "Parallel Smash", "drive=30 mix=0.35 output=-8" },
         { "Drive", "Asymmetric Bite", "drive=18 bias=-0.6 color=0.2 output=-7" },
         { "Saturator", "Warm Tube", "model1=1 amount1=0.25 mix=0.7" },
         { "Saturator", "Tape Bus", "model1=2 amount1=0.35 output=-3" },
         { "Saturator", "Tube into Tape", "model1=1 amount1=0.4 model2=2 amount2=0.3 output=-5" },
         { "Saturator", "Band Crunch", "routing=3 model1=1 amount1=0.2 model2=3 amount2=0.5 model3=2 amount3=0.3 output=-6" },
         { "Saturator", "Wide Grit", "routing=2 model1=2 amount1=0.2 model2=4 amount2=0.4 output=-4" },
         { "Saturator", "Feedback Fuzz", "routing=4 feedback=0.8 model1=3 amount1=0.7 model2=4 amount2=0.4 output=-10" },
         { "Saturator", "Lo-Fi Crush", "model1=6 amount1=0.6 model2=2 amount2=0.2 mix=0.5" },
         { "Saturator", "Octave Fuzz", "model1=5 amount1=0.4 model2=1 amount2=0.3 mix=0.4 output=-4" },

         { "Dynamics", "Gentle Glue", "threshold=-14 ratio=2 attack=30 release=200 makeup=2 detectorRms=1 style=1" },
         { "Dynamics", "Vocal Level", "threshold=-20 ratio=3 attack=8 release=120 makeup=4 detectorRms=1 style=2" },
         { "Dynamics", "Drum Punch", "threshold=-16 ratio=4 attack=30 release=80 makeup=3 style=3" },
         { "Dynamics", "Fast Catch", "threshold=-12 ratio=8 attack=0.5 release=50 makeup=2" },
         { "Dynamics", "Brick Squash", "threshold=-30 ratio=20 attack=1 release=150 makeup=10" },
         { "Dynamics", "Bass Tame", "threshold=-18 ratio=5 attack=15 release=180 makeup=3 detectorRms=1 scHpf=1" },
         { "Dynamics", "Parallel Crush", "threshold=-36 ratio=12 attack=2 release=90 makeup=12 mix=0.4" },
         { "Dynamics", "Slow Bus", "threshold=-10 ratio=1.5 attack=80 release=600 makeup=1 detectorRms=1 analog=1 style=2" },

         { "EQ", "Rumble Trim", "band1Freq=60 band1Gain=-12" },
         { "EQ", "Warmth", "band1Gain=3 band2Freq=200 band2Gain=2 band5Gain=-2" },
         { "EQ", "Presence", "band4Freq=3500 band4Q=0.8 band4Gain=4" },
         { "EQ", "Air", "band5Freq=12000 band5Gain=5" },
         { "EQ", "Scoop", "band1Gain=3 band3Freq=600 band3Q=0.7 band3Gain=-6 band5Gain=3" },
         { "EQ", "Telephone", "band1Freq=400 band1Gain=-24 band3Freq=1500 band3Q=0.8 band3Gain=6 band5Freq=3000 band5Gain=-24" },
         { "EQ", "Mud Cut", "band2Freq=300 band2Q=1.4 band2Gain=-5" },
         { "EQ", "Bass Boost", "band1Freq=90 band1Gain=6 band2Freq=250 band2Gain=-2" },
         { "EQ", "Vocal Shine", "band2Freq=250 band2Gain=-3 band4Freq=5000 band4Gain=3 band5Gain=4" },
      };

      struct Table
      {
         std::vector<std::string> types;
         std::map<std::string, std::vector<Preset>> byType;
      };

      const Table& Get()
      {
         static const Table table = [] {
            Table t;
            for (const Row& row : kRows)
            {
               if (t.byType.find(row.type) == t.byType.end())
                  t.types.push_back(row.type);
               Preset p;
               p.name = row.name;
               std::istringstream in(row.values);
               std::string pair;
               while (in >> pair)
               {
                  const size_t eq = pair.find('=');
                  if (eq != std::string::npos)
                     p.values.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
               }
               t.byType[row.type].push_back(std::move(p));
            }
            return t;
         }();
         return table;
      }
   }

   const std::vector<std::string>& Types() { return Get().types; }

   const std::vector<Preset>* For(const std::string& typeName)
   {
      const auto it = Get().byType.find(typeName);
      return it == Get().byType.end() ? nullptr : &it->second;
   }

   bool Apply(INode* node, const std::string& typeName, int index)
   {
      const std::vector<Preset>* list = For(typeName);
      if (node == nullptr || list == nullptr || index < 0 || index >= (int)list->size())
         return false;
      std::vector<std::pair<std::string, std::string>> values;
      {
         std::unique_ptr<INode> fresh(NodeFactory::Instance().MakeNode(typeName));
         if (!fresh)
            return false;
         Patch::SaveParams(fresh.get(), values);
      }
      for (const auto& kv : (*list)[(size_t)index].values)
      {
         // Saved keys carry a one-letter type tag ("f cutoff"); the table names
         // the param only, so a preset cannot disagree with the node on type.
         for (auto& have : values)
            if (have.first.size() > 2 && have.first.compare(2, std::string::npos, kv.first) == 0)
               have.second = kv.second;
      }
      Patch::LoadParams(node, values);
      return true;
   }
}
