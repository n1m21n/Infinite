// find-eval: how often `infinite find` puts a right node in its top 3, over a file of real asks.
// Usage: find-eval <asks.tsv> [min-hits]   (exit 1 when the hits fall below min-hits)
// tests/chat: find_eval.tsv is what the finder is tuned on, find_holdout.tsv is never tuned on, find_glossary.tsv is
// musician and visual-artist words, find_nodes.tsv asks for every node without its name (coverage).
// 2026-11-05: eval 75/75, holdout 16/20, glossary 221/222, nodes 254/310 (help text ranked). Pass these as min-hits to catch a regression.
#include "core/AgentDocs.h"
#include "core/chat/NodeFinder.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
   if (argc < 2)
   {
      fprintf(stderr, "usage: find-eval <asks.tsv> [min-hits]\n");
      return 2;
   }
   std::ifstream f(argv[1]);
   if (!f)
   {
      fprintf(stderr, "find-eval: cannot read %s\n", argv[1]);
      return 2;
   }
   // A right node: its card or type, any case, or a node one card covers ("ripple" in "Twirl, Pinch/Punch & Ripple").
   auto norm = [](const std::string& s) {
      std::string o;
      for (char c : s)
         if (isalnum((unsigned char)c))
            o += (char)tolower((unsigned char)c);
      return o;
   };
   auto right = [&](const std::string& want, const chat::NodeHit& h) {
      size_t a = 0;
      while (a <= want.size())
      {
         size_t b = want.find('|', a);
         if (b == std::string::npos)
            b = want.size();
         const std::string w = norm(want.substr(a, b - a));
         a = b + 1;
         if (w.empty())
            continue;
         if (w == norm(h.name) || w == norm(h.type))
            return true;
         std::vector<std::string> parts;
         std::string cur;
         for (char c : h.name + " ")
            if (isalnum((unsigned char)c))
               cur += (char)tolower((unsigned char)c);
            else if (!cur.empty())
               parts.push_back(cur), cur.clear();
         if (parts.size() > 1)
            for (size_t i = 0; i < parts.size(); i++)
            {
               std::string run;
               for (size_t j = i; j < parts.size(); j++)
                  if ((run += parts[j]) == w)
                     return true;
            }
      }
      return false;
   };
   std::string line;
   int n = 0, hit = 0;
   while (std::getline(f, line))
   {
      const size_t tab = line.find('\t');
      if (line.empty() || line[0] == '#' || tab == std::string::npos)
         continue;
      const std::string ask = line.substr(0, tab), want = line.substr(tab + 1);
      bool ok = false;
      std::string got;
      for (const chat::NodeHit& h : chat::FindNodes(ask, AgentDocs::kNodeGuide, 3))
      {
         ok = ok || right(want, h);
         got += " " + h.name + ";";
      }
      n++;
      hit += ok;
      if (!ok)
         printf("miss  %-44s ->%s\n", ask.c_str(), got.c_str());
   }
   printf("top-3 %d/%d\n", hit, n);
   return argc > 2 && hit < atoi(argv[2]) ? 1 : 0;
}
