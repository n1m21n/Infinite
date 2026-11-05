// find-eval: how often `infinite find` puts a right node in its top 3, over a file of real asks.
// Usage: find-eval <asks.tsv> [min-hits]   (exit 1 when the hits fall below min-hits)
// tests/chat/find_eval.tsv is what the finder is tuned on; tests/chat/find_holdout.tsv is never tuned on.
#include "core/AgentDocs.h"
#include "core/chat/NodeFinder.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

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
   std::string line;
   int n = 0, hit = 0;
   while (std::getline(f, line))
   {
      const size_t tab = line.find('\t');
      if (line.empty() || line[0] == '#' || tab == std::string::npos)
         continue;
      const std::string ask = line.substr(0, tab), want = "|" + line.substr(tab + 1) + "|";
      bool ok = false;
      std::string got;
      for (const chat::NodeHit& h : chat::FindNodes(ask, AgentDocs::kNodeGuide, 3))
      {
         ok = ok || want.find("|" + h.name + "|") != std::string::npos;
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
