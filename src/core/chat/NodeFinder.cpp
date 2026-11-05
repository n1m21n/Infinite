#include "core/chat/NodeFinder.h"
#include "core/chat/NodeVocab.gen.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace chat
{
   namespace
   {
      std::string Lower(std::string s)
      {
         std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return s;
      }

      std::vector<std::string> Split(const std::string& text)
      {
         std::vector<std::string> out;
         std::string w;
         for (size_t i = 0; i <= text.size(); i++)
         {
            const unsigned char c = i < text.size() ? (unsigned char)text[i] : ' ';
            if (std::isalnum(c))
               w += (char)std::tolower(c);
            else if (!w.empty())
            {
               out.push_back(w);
               w.clear();
            }
         }
         return out;
      }

      // Two words are one when they share all but a short ending: arpeggiate / arpeggiator, mix / mixer, slice / slicer,
      // smooth / smoothing. Not react / reaction: the card word may run at most `slack` letters past the shared part.
      bool Same(const std::string& ask, const std::string& card, size_t slack)
      {
         if (ask == card)
            return true;
         size_t k = 0;
         while (k < ask.size() && k < card.size() && ask[k] == card[k])
            k++;
         if (k < 3 || (k < ask.size() && k < 4))
            return false;
         // The ask may only run past the card word by an ending ("blurs", "delayed"), never a new word ("projector").
         static const std::set<std::string> kEnds = { "", "s", "es", "ed", "d", "e", "er", "ly", "y", "ing" };
         // The card word may run past the whole ask by an ending too ("smooth" is Smoothing).
         if (k == ask.size() && kEnds.count(card.substr(k)) > 0)
            return true;
         return kEnds.count(ask.substr(k)) > 0 && card.size() - k <= slack;
      }

      // Everyday words the cards say another way. Each ask word also looks for these, at half weight.
      const std::map<std::string, std::vector<std::string>>& Bridges()
      {
         static const std::map<std::string, std::vector<std::string>> kBridges = {
            { "voice", { "microphone", "input", "record" } }, { "vocal", { "microphone", "input" } },
            { "vocals", { "microphone", "input" } }, { "sing", { "microphone", "input" } },
            { "mic", { "microphone", "input" } }, { "spacious", { "reverb", "room" } }, { "space", { "reverb" } },
            { "ambience", { "reverb" } }, { "crunchy", { "distortion", "saturation", "bitcrush" } },
            { "dirty", { "distortion", "saturation" } }, { "gritty", { "distortion", "saturation" } },
            { "distort", { "distortion", "saturation", "displace", "liquify" } }, { "louder", { "gain", "volume" } },
            { "quieter", { "gain", "volume" } }, { "loud", { "gain" } }, { "projector", { "projection", "output" } }, { "display", { "output", "window" } }, { "monitor", { "output" } }, { "phone", { "osc" } }, { "ipad", { "osc" } }, { "tablet", { "osc" } },
            { "gradient", { "ramp" } }, { "app", { "syphon", "ndi" } }, { "apps", { "syphon", "ndi" } }, { "knob", { "macro" } },
            { "wobble", { "lfo", "oscillation" } }, { "sway", { "lfo" } }, { "mix", { "mixer", "blend" } },
            { "combine", { "blend", "layer", "mixer" } }, { "photo", { "image" } }, { "picture", { "image" } },
            { "pan", { "spatial", "stereo" } }, { "jittery", { "smoothing" } }, { "jitter", { "smoothing" } },
            { "motion", { "optical", "flow" } }, { "visualize", { "audio", "reactive", "displacement" } },
            { "visualise", { "audio", "reactive", "displacement" } }, { "visualizer", { "audio", "reactive" } },
            { "correct", { "adjustments", "curves", "grade" } }, { "grade", { "adjustments", "curves", "lut" } },
            { "key", { "scale", "snap" } }, { "scale", { "key", "snap" } }, { "render", { "render" } },
            { "webcam", { "camera", "video" } }, { "code", { "sketch", "javascript", "kernel", "glsl" } },
            { "p5", { "sketch" } }, { "processing", { "sketch" } }, { "shader", { "glsl", "kernel", "pixel" } }, { "beat", { "drum", "rhythm" } }, { "chop", { "slice", "slices" } },
            // Musician words no card uses (tests/chat/find_glossary.tsv).
            { "lush", { "reverb", "chorus", "wavetable" } }, { "pad", { "wavetable", "analog", "reverb" } },
            { "pads", { "wavetable", "analog", "reverb" } }, { "supersaw", { "analog", "unison", "detune" } },
            { "hats", { "drum", "metallic" } }, { "hihat", { "drum", "metallic" } }, { "cymbal", { "metallic", "drum" } },
            { "resonance", { "filter", "resonator" } }, { "resonant", { "filter", "resonator" } },
            { "compressor", { "dynamics", "limiter" } }, { "compress", { "dynamics" } }, { "compression", { "dynamics" } },
            { "overdrive", { "drive", "saturator" } }, { "distortion", { "drive", "saturator" } },
            { "lofi", { "bitcrush", "saturator" } }, { "panning", { "stereo", "spatial" } },
            { "autotune", { "key", "snap", "pitch" } }, { "octave", { "transpose", "pitch" } },
            { "octaves", { "transpose", "pitch" } }, { "tuning", { "transpose", "snap", "pitch" } },
            { "tune", { "transpose", "snap", "pitch" } }, { "arpeggio", { "arpeggiator" } },
            { "arp", { "arpeggiator" } }, { "modulation", { "mod", "lfo", "envelope", "mseg" } }, { "modulate", { "lfo", "envelope" } },
            { "follow", { "analyze", "track" } }, { "arrange", { "piano", "roll", "sequencer" } },
            { "arranging", { "piano", "roll", "sequencer" } }, { "arrangement", { "piano", "roll" } },
            { "record", { "audio", "looper" } }, { "wav", { "audio", "file", "sampler" } }, { "mp3", { "audio", "file" } },
            // Visual-artist words no card uses.
            { "glsl", { "shadertoy" } }, { "graphics", { "shape", "draw" } }, { "typography", { "text" } },
            { "font", { "text" } }, { "duotone", { "gradientmap", "gradient" } }, { "grain", { "noise" } },
            { "sharpen", { "unsharp", "unsharpmask" } }, { "pixel", { "pixelate", "dither" } }, { "warp", { "liquify", "displace" } },
            { "swirl", { "twirl" } }, { "fisheye", { "lens", "lensdistortion" } },
            { "shadow", { "dropshadow" } }, { "mask", { "key", "alpha", "matte" } }, { "transparency", { "opacity", "alpha" } },
            { "transparent", { "opacity", "alpha" } }, { "rotate", { "transform" } }, { "rotation", { "transform" } },
            { "smoke", { "fluid" } }, { "ink", { "fluid" } }, { "mesh", { "geometry", "edit" } },
            { "lowpoly", { "decimate", "triangulate" } }, { "poly", { "decimate", "triangulate" } },
            { "boolean", { "difference", "union", "intersect" } }, { "physics", { "rigid", "cloth" } },
            { "rig", { "armature" } }, { "skeleton", { "armature" } }, { "bones", { "armature" } },
            // Effect words people use for what a node does (tests/chat/find_nodes.tsv).
            { "peaks", { "limiter", "dynamics" } }, { "clipping", { "limiter" } }, { "squash", { "dynamics", "compressor" } },
            { "mids", { "eq" } }, { "treble", { "eq" } }, { "highs", { "eq", "filter" } },
            { "harder", { "transient" } }, { "punch", { "transient" } }, { "whoosh", { "flanger" } }, { "jet", { "flanger" } },
            { "notch", { "phaser" } }, { "thicken", { "chorus" } }, { "doubled", { "chorus" } }, { "warm", { "saturator", "analog" } },
            { "robotic", { "ring" } }, { "static", { "noise", "addnoise" } }, { "corners", { "vignette" } },
            { "blocky", { "pixelate" } }, { "negative", { "invert" } }, { "brighter", { "exposure" } }, { "darker", { "exposure" } },
            { "tint", { "coloroverlay", "overlay" } }, { "comic", { "halftone" } }, { "barrel", { "lensdistortion", "lens" } },
            { "smear", { "liquify" } }, { "past", { "machine" } }, { "fold", { "symmetry" } }, { "chunks", { "stutter" } },
            { "harmonics", { "partials" } }, { "freeze", { "bake" } }, { "overlap", { "intersect" } },
            { "subtract", { "difference" } }, { "shiny", { "material" } }, { "metal", { "material", "metallic" } },
            { "liquid", { "fluid" } }, { "loudness", { "analyze" } }, { "interface", { "audio", "in" } },
            { "listener", { "spatial" } }, { "kernel", { "convolve" } }, { "building", { "projection" } },
            // Shape and object words the node names say another way.
            { "oval", { "ellipse" } }, { "lamp", { "light" } }, { "donut", { "torus", "ring" } }, { "doughnut", { "torus", "ring" } },
            { "ball", { "sphere" } }, { "coil", { "helix" } }, { "spring", { "helix" } }, { "cog", { "gear" } },
            { "box", { "rectangle", "cube" } }, { "floor", { "plane" } }, { "ground", { "plane" } }, { "lettering", { "text" } },
            { "words", { "text" } }, { "round", { "circle", "sphere" } }, { "pill", { "capsule" } }, { "square", { "rectangle" } },
            { "reactive", { "analyze" } }, { "spectrum", { "analyze", "spectral" } }, { "waveform", { "meter", "ribbon", "scope" } },
         };
         return kBridges;
      }

      // Words that only say "please" or "a thing", never which node.
      bool Stop(const std::string& w)
      {
         static const std::set<std::string> kStop = {
            "the", "and", "for", "with", "that", "this", "into", "from", "make", "want", "need", "some", "can", "how",
            "what", "add", "use", "node", "nodes", "like", "get", "put", "my", "me", "it", "to", "of", "an", "in", "on",
            "is", "be", "do", "you", "or", "so", "out", "up", "let", "lets", "please", "something", "thing", "things",
            "feel", "look", "looks", "sound", "sounds", "over", "time", "around", "behind", "just", "really", "very",
            "would", "could", "should", "will", "turn", "one", "two", "super", "kind", "sort", "way", "bit", "little",
            "show", "top", "gets", "get", "bigger", "smaller", "more", "less", "when", "between", "all", "there", "here",
         };
         return w.size() < 2 || kStop.count(w) > 0;
      }

      struct Card
      {
         NodeHit hit;
         std::vector<std::string> name, kind, purpose, body, params;
         int domain = 0; // 1 sound, 2 picture, 0 either
         bool bare = false; // no guide card: ranked on its controls alone
      };

      bool In(const std::vector<std::string>& words, const std::string& w, size_t slack)
      {
         return std::any_of(words.begin(), words.end(), [&](const std::string& c) { return Same(w, c, slack); });
      }

      // Letters and digits only, lowercase: "Motion Blur" and "motionblur" are one name.
      std::string Norm(const std::string& s)
      {
         std::string o;
         for (char ch : s)
            if (std::isalnum((unsigned char)ch))
               o += (char)std::tolower((unsigned char)ch);
         return o;
      }

      // The type a card is about: the same name ("Motion Blur" = motionblur), a name its words hold ("Wavetable Synth"
      // holds Wavetable), or one of its words joined to the last ("Gaussian / Box Blur" = gaussianblur). The longest wins.
      const NodeVocab::Entry* TypeOfCard(const std::string& title, const std::vector<std::string>& words)
      {
         const std::string n = Norm(title);
         const NodeVocab::Entry* best = nullptr;
         size_t bestLen = 0;
         for (const NodeVocab::Entry& e : NodeVocab::kEntries)
         {
            const std::string t = Norm(e.type);
            if (t == n)
               return &e;
            bool fits = false;
            std::vector<std::string> tw = Split(e.type);
            if (!tw.empty())
               fits = std::all_of(tw.begin(), tw.end(), [&](const std::string& w) { return std::find(words.begin(), words.end(), w) != words.end(); });
            if (!fits && words.size() > 1)
               for (size_t i = 0; i + 1 < words.size() && !fits; i++)
                  fits = t == words[i] + words.back();
            if (fits && t.size() > bestLen)
               best = &e, bestLen = t.size();
         }
         if (best != nullptr)
            return best;
         // Last: the card's words name part of a type ("Ramp" is Color Ramp); the shortest such type.
         for (const NodeVocab::Entry& e : NodeVocab::kEntries)
         {
            const std::vector<std::string> tw = Split(e.type);
            const bool holds = !words.empty() && std::all_of(words.begin(), words.end(), [&](const std::string& w) {
               return std::find(tw.begin(), tw.end(), w) != tw.end();
            });
            if (holds && (best == nullptr || std::strlen(e.type) < std::strlen(best->type)))
               best = &e;
         }
         return best;
      }

      // A category's stock help text (many types share it) says nothing about which of them fits.
      bool SharedHelp(const char* help)
      {
         if (help == nullptr || *help == '\0')
            return true;
         int n = 0;
         for (const NodeVocab::Entry& e : NodeVocab::kEntries)
            n += std::strcmp(e.help, help) == 0;
         return n > 2;
      }

      int DomainOfCategory(const std::string& c)
      {
         if (c == "Synths" || c == "AudioEffects" || c == "Notes")
            return 1;
         if (c == "3D" || c == "Compositing" || c == "Effects" || c == "Source")
            return 2;
         return 0;
      }

      int AskDomain(const std::vector<std::string>& words)
      {
         static const std::set<std::string> kSound = { "sound", "audio", "music", "synth", "drum", "drums", "bass", "kick",
                                                       "snare", "voice", "vocal", "vocals", "mic", "sing", "song", "loop",
                                                       "sample", "melody", "chord", "chords", "notes", "note", "louder",
                                                       "quieter", "crunchy", "reverb", "echo", "tracks", "plucked", "bell" };
         static const std::set<std::string> kPicture = { "visual", "visuals", "image", "images", "picture", "photo",
                                                         "photos", "video", "color", "colour", "pixel", "pixels", "shader",
                                                         "3d", "mesh", "screen", "glow", "blur", "scene", "texture",
                                                         "background", "projector", "webcam", "edges", "look" };
         int sound = 0, picture = 0;
         for (const std::string& w : words)
            sound += kSound.count(w) > 0, picture += kPicture.count(w) > 0;
         return sound && picture ? 3 : sound ? 1 : picture ? 2 : 0;
      }
   }

   std::vector<NodeHit> FindNodes(const std::string& ask, const std::string& guide, size_t limit)
   {
      const std::vector<std::string> raw = Split(ask);
      std::vector<std::string> words;
      for (const std::string& w : raw)
         if (!Stop(w))
            words.push_back(w);
      std::vector<NodeHit> hits;
      if (words.empty())
         return hits;

      static std::vector<Card> cards; // the guide is a constant: read once
      static const std::string* readFrom = nullptr;
      if (readFrom != &guide || cards.empty())
      {
         cards.clear();
         readFrom = &guide;
         for (size_t at = guide.find("### "); at != std::string::npos;)
         {
            const size_t next = guide.find("\n### ", at + 4);
            const std::string text = guide.substr(at, next == std::string::npos ? std::string::npos : next - at);
            at = next == std::string::npos ? next : next + 1;
            Card c;
            const size_t nl = text.find('\n');
            c.hit.name = text.substr(4, nl == std::string::npos ? std::string::npos : nl - 4);
            const std::string body = nl == std::string::npos ? std::string() : text.substr(nl + 1);
            const size_t dot = body.find(". ");
            c.hit.kind = body.substr(0, dot);
            const size_t end = dot == std::string::npos ? dot : body.find(". ", dot + 2);
            c.hit.purpose = dot == std::string::npos ? std::string() : body.substr(dot + 2, end == std::string::npos ? std::string::npos : end - dot - 1);
            if (const size_t cut = c.hit.purpose.find('\n'); cut != std::string::npos)
               c.hit.purpose.resize(cut);
            c.name = Split(c.hit.name);
            c.kind = Split(c.hit.kind);
            c.purpose = Split(c.hit.purpose);
            c.body = Split(body);
            const std::string k = Lower(c.hit.kind);
            const bool audio = k.find("audio") != std::string::npos || k.find("synth") != std::string::npos || k.find("note") != std::string::npos;
            const bool image = k.find("image") != std::string::npos || k.find("3d") != std::string::npos || k.find("geometry") != std::string::npos ||
                               k.find("compositing") != std::string::npos || k.find("effects") == 0 || k.find("source") == 0 ||
                               k.find("color") == 0 || k.find("text") == 0;
            c.domain = audio && !image ? 1 : image && !audio ? 2 : 0;
            // The guide also holds language and manual sections; a card is a node when its kind line reads "KIND · IO".
            const NodeVocab::Entry* type = TypeOfCard(c.hit.name, c.name);
            if (type == nullptr && c.hit.kind.find("\xc2\xb7") == std::string::npos)
               continue;
            if (type != nullptr)
            {
               c.hit.type = type->type;
               c.params = Split(type->words);
               if (!SharedHelp(type->help))
                  for (std::string& w : Split(type->help))
                     c.body.push_back(std::move(w));
               if (c.domain == 0)
                  c.domain = DomainOfCategory(type->category);
            }
            cards.push_back(std::move(c));
         }
         // Nodes with no card: their name, category and control words.
         for (const NodeVocab::Entry& e : NodeVocab::kEntries)
         {
            if (std::any_of(cards.begin(), cards.end(), [&](const Card& c) { return c.hit.type == e.type; }))
               continue;
            Card c;
            c.hit.name = c.hit.type = e.type;
            c.hit.kind = e.category;
            c.name = Split(e.type);
            c.kind = Split(e.category);
            c.params = Split(e.words);
            c.domain = DomainOfCategory(e.category);
            c.bare = true;
            // Its help text is the card it lacks: ranked on, and quoted by its first sentence.
            if (!SharedHelp(e.help))
               c.body = Split(e.help);
            // No card to quote: its first controls say what it is ("Controls: rate, depth, feedback").
            std::string ctl;
            for (size_t i = 0; i < c.params.size() && i < 8; i++)
               ctl += (i ? ", " : "") + c.params[i];
            if (!ctl.empty())
               c.hit.purpose = "Controls: " + ctl + ".";
            cards.push_back(std::move(c));
         }
      }

      // Rare words say more: a word on every card ("audio") counts for little, one on three cards for a lot.
      auto idf = [&](const std::string& w)
      {
         int df = 0;
         for (const Card& c : cards)
            df += In(c.body, w, 3) || In(c.name, w, 2) || In(c.params, w, 2);
         return std::log((cards.size() + 1.0) / (df + 1.0));
      };
      struct Term
      {
         std::string word;
         double weight;
         int from; // which ask word it stands for
      };
      std::vector<Term> terms;
      for (size_t i = 0; i < words.size(); i++)
      {
         terms.push_back({ words[i], idf(words[i]), (int)i });
         if (auto b = Bridges().find(words[i]); b != Bridges().end())
            for (const std::string& alt : b->second)
               terms.push_back({ alt, 0.8 * idf(alt), (int)i });
      }
      const int domain = AskDomain(raw);

      for (const Card& c : cards)
      {
         std::vector<double> best(words.size(), 0.0);
         for (const Term& t : terms)
         {
            // A name hit counts even for a common word: "image" is everywhere, but Image Source is still the image node.
            const double w = std::max(t.weight, 1.5);
            const double field = In(c.name, t.word, 2) ? 6.0 * w : In(c.kind, t.word, 2) ? 2.0 * t.weight
                               : In(c.purpose, t.word, 3) ? 2.5 * t.weight : In(c.params, t.word, 2) ? 1.5 * t.weight
                               : In(c.body, t.word, 3) ? 1.0 * t.weight : 0.0;
            best[t.from] = std::max(best[t.from], field);
         }
         double score = 0.0;
         int matched = 0;
         for (double b : best)
            score += b, matched += b > 0.0;
         if (matched == 0)
            continue;
         // A card that meets more of the ask beats one that meets a single word many ways.
         score *= 1.0 + 0.5 * (matched - 1);
         // The ask names this node outright: every word of its name is in the ask ("render the 3d scene", "mixer").
         int named = 0, nameWords = 0;
         for (const std::string& n : c.name)
            if (n.size() > 1)
            {
               nameWords++;
               named += std::any_of(terms.begin(), terms.end(), [&](const Term& t) { return Same(t.word, n, 2); });
            }
         if (nameWords > 0 && named == nameWords)
            score *= 1.6;
         // Sound asks want sound nodes, picture asks picture nodes; an ask that says both wants the bridges.
         if (domain == 1 || domain == 2)
            score *= c.domain == domain ? 1.3 : c.domain == 0 ? 1.0 : 0.6;
         else if (domain == 3)
            score *= c.domain == 0 ? 1.3 : 1.0;
         // A node the guide describes is the surer pick over one known only by its controls.
         if (c.bare)
            score *= 0.85;
         NodeHit h = c.hit;
         h.score = (int)std::lround(score * 10.0);
         hits.push_back(h);
      }
      std::stable_sort(hits.begin(), hits.end(), [](const NodeHit& a, const NodeHit& b) { return a.score > b.score; });
      // Two cards for one node (an OS variant, a section) show once, at the better score.
      std::set<std::string> seen;
      hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const NodeHit& h) {
         return !seen.insert(h.type.empty() ? h.name : h.type).second;
      }), hits.end());
      if (hits.size() > limit)
         hits.resize(limit);
      return hits;
   }

   std::string NodeDoc(const std::string& name, const std::string& guide)
   {
      const std::string want = Norm(name);
      if (want.empty())
         return {};
      // The guide's node cards, title and text.
      std::vector<std::pair<std::string, std::string>> cardsOf;
      for (size_t at = guide.find("### "); at != std::string::npos;)
      {
         const size_t next = guide.find("\n### ", at + 4);
         const std::string text = guide.substr(at, next == std::string::npos ? std::string::npos : next - at);
         at = next == std::string::npos ? next : next + 1;
         const size_t nl = text.find('\n');
         cardsOf.push_back({ text.substr(4, nl == std::string::npos ? std::string::npos : nl - 4), text });
      }
      const NodeVocab::Entry* type = nullptr;
      for (const NodeVocab::Entry& e : NodeVocab::kEntries)
         if (Norm(e.type) == want)
            type = &e;
      std::string card;
      for (const auto& [title, text] : cardsOf)
         if (Norm(title) == want)
         {
            card = text;
            if (type == nullptr)
               type = TypeOfCard(title, Split(title));
            break;
         }
      // Asked by type ("Wavetable"): the card about that type ("Wavetable Synth").
      if (card.empty() && type != nullptr)
         for (const auto& [title, text] : cardsOf)
            if (TypeOfCard(title, Split(title)) == type)
            {
               card = text;
               break;
            }
      if (card.empty() && type == nullptr)
         return {};
      std::string out = card.empty() ? "### " + std::string(type->type) + "\n" + type->category + ".\n" : card;
      while (!out.empty() && out.back() == '\n')
         out.pop_back();
      out += "\n";
      if (type != nullptr)
      {
         if (type->help[0] != '\0' && card.find(type->help) == std::string::npos)
            out += std::string("Help: ") + type->help + "\n";
         if (type->words[0] != '\0')
         {
            std::string ctl;
            for (const char* c = type->words; *c != '\0'; c++)
               ctl += *c == ' ' ? std::string(", ") : std::string(1, *c);
            out += "Control words: " + ctl + "\n";
         }
         out += std::string("Add as type `") + type->type + "`; params and ranges: `infinite describe " + type->type + "`.\n";
      }
      return out;
   }
}
