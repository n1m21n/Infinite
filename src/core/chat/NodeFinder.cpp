#include "core/chat/NodeFinder.h"
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
            { "distort", { "distortion", "saturation" } }, { "louder", { "gain", "volume" } },
            { "quieter", { "gain", "volume" } }, { "loud", { "gain" } }, { "projector", { "projection", "output" } }, { "display", { "output", "window" } }, { "monitor", { "output" } }, { "phone", { "osc" } }, { "ipad", { "osc" } }, { "tablet", { "osc" } },
            { "app", { "syphon", "ndi" } }, { "apps", { "syphon", "ndi" } }, { "knob", { "macro" } },
            { "wobble", { "lfo", "oscillation" } }, { "sway", { "lfo" } }, { "mix", { "mixer", "blend" } },
            { "combine", { "blend", "layer", "mixer" } }, { "photo", { "image" } }, { "picture", { "image" } },
            { "pan", { "spatial", "stereo" } }, { "jittery", { "smoothing" } }, { "jitter", { "smoothing" } },
            { "motion", { "optical", "flow" } }, { "visualize", { "audio", "reactive", "displacement" } },
            { "visualise", { "audio", "reactive", "displacement" } }, { "visualizer", { "audio", "reactive" } },
            { "correct", { "adjustments", "curves", "grade" } }, { "grade", { "adjustments", "curves", "lut" } },
            { "key", { "scale", "snap" } }, { "scale", { "key", "snap" } }, { "render", { "render" } },
            { "webcam", { "camera", "video" } }, { "code", { "sketch", "javascript", "kernel", "glsl" } },
            { "p5", { "sketch" } }, { "processing", { "sketch" } }, { "shader", { "glsl", "kernel", "pixel" } }, { "beat", { "drum", "rhythm" } }, { "chop", { "slice", "slices" } },
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
         std::vector<std::string> name, kind, purpose, body;
         int domain = 0; // 1 sound, 2 picture, 0 either
      };

      bool In(const std::vector<std::string>& words, const std::string& w, size_t slack)
      {
         return std::any_of(words.begin(), words.end(), [&](const std::string& c) { return Same(w, c, slack); });
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
            cards.push_back(std::move(c));
         }
      }

      // Rare words say more: a word on every card ("audio") counts for little, one on three cards for a lot.
      auto idf = [&](const std::string& w)
      {
         int df = 0;
         for (const Card& c : cards)
            df += In(c.body, w, 3) || In(c.name, w, 2);
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
                               : In(c.purpose, t.word, 3) ? 2.5 * t.weight : In(c.body, t.word, 3) ? 1.0 * t.weight : 0.0;
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
         NodeHit h = c.hit;
         h.score = (int)std::lround(score * 10.0);
         hits.push_back(h);
      }
      std::stable_sort(hits.begin(), hits.end(), [](const NodeHit& a, const NodeHit& b) { return a.score > b.score; });
      if (hits.size() > limit)
         hits.resize(limit);
      return hits;
   }
}
