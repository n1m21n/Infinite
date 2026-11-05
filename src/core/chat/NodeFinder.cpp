#include "core/chat/NodeFinder.h"
#include <algorithm>
#include <cctype>
#include <cstring>
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

      // Words of the ask, folded to a stem so "blurring", "blurred" and "blurs" all meet "blur".
      std::string Stem(std::string w)
      {
         for (const char* end : { "ing", "ed", "es", "s" })
         {
            const size_t n = strlen(end);
            if (w.size() > n + 3 && w.compare(w.size() - n, n, end) == 0)
            {
               w.resize(w.size() - n);
               break;
            }
         }
         return w;
      }

      std::vector<std::string> Words(const std::string& text)
      {
         static const std::set<std::string> kStop = { "the", "and", "for", "with", "that", "this", "into", "from", "make",
                                                      "want", "need", "some", "can", "how", "what", "add", "use", "node",
                                                      "nodes", "like", "get", "put", "my", "me", "it", "to", "of", "a",
                                                      "an", "in", "on", "is", "be", "do", "i", "you", "or", "so", "out" };
         std::vector<std::string> out;
         std::string w;
         for (size_t i = 0; i <= text.size(); i++)
         {
            const unsigned char c = i < text.size() ? (unsigned char)text[i] : ' ';
            if (std::isalnum(c))
               w += (char)std::tolower(c);
            else if (!w.empty())
            {
               if (!kStop.count(w) && w.size() > 1)
                  out.push_back(Stem(w));
               w.clear();
            }
         }
         return out;
      }

      // Word-start match: "blur" hits "blur" and "blurry", not "unblurred" mid-word. `slack` caps how many letters the
      // hay word may run past the stem: names want near-whole words ("slice" is Slicer, "react" is not Reaction).
      bool HasWord(const std::string& hay, const std::string& stem, size_t slack = std::string::npos)
      {
         for (size_t at = hay.find(stem); at != std::string::npos; at = hay.find(stem, at + 1))
         {
            if (at != 0 && std::isalnum((unsigned char)hay[at - 1]))
               continue;
            size_t end = at + stem.size();
            while (end < hay.size() && std::isalnum((unsigned char)hay[end]))
               end++;
            if (end - at - stem.size() <= slack)
               return true;
         }
         return false;
      }
   }

   std::vector<NodeHit> FindNodes(const std::string& ask, const std::string& guide, size_t limit)
   {
      const std::vector<std::string> words = Words(ask);
      std::vector<NodeHit> hits;
      if (words.empty())
         return hits;
      for (size_t at = guide.find("### "); at != std::string::npos;)
      {
         const size_t next = guide.find("\n### ", at + 4);
         const std::string card = guide.substr(at, next == std::string::npos ? std::string::npos : next - at);
         at = next == std::string::npos ? next : next + 1;

         const size_t nl = card.find('\n');
         NodeHit h;
         h.name = card.substr(4, nl == std::string::npos ? std::string::npos : nl - 4);
         const std::string body = nl == std::string::npos ? std::string() : card.substr(nl + 1);
         const size_t dot = body.find(". ");
         h.kind = body.substr(0, dot);
         const size_t end = dot == std::string::npos ? dot : body.find(". ", dot + 2);
         h.purpose = dot == std::string::npos ? std::string() : body.substr(dot + 2, end == std::string::npos ? std::string::npos : end - dot - 1);
         if (const size_t cut = h.purpose.find('\n'); cut != std::string::npos)
            h.purpose.resize(cut);

         const std::string name = Lower(h.name), kind = Lower(h.kind), text = Lower(body);
         int matched = 0;
         for (const std::string& w : words)
         {
            int s = 0;
            if (HasWord(name, w, 2))
               s = 6;
            else if (HasWord(kind, w))
               s = 3;
            else if (HasWord(Lower(h.purpose), w))
               s = 2;
            else if (HasWord(text, w))
               s = 1;
            h.score += s;
            matched += s > 0;
         }
         // A card that meets more of the ask's words beats one that meets a single word many ways.
         h.score += matched * matched;
         if (matched > 0)
            hits.push_back(h);
      }
      std::stable_sort(hits.begin(), hits.end(), [](const NodeHit& a, const NodeHit& b) { return a.score > b.score; });
      if (hits.size() > limit)
         hits.resize(limit);
      return hits;
   }
}
