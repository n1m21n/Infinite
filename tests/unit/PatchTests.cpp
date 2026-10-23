// Turbo 0.70 (A7): the .inf reader and writer (src/core/Patch.cpp), rule R1:
// a saved patch reads back unchanged, older and upstream files keep opening.
// Format reference: docs/patch-format.md.
#include "doctest.h"

#include <string>
#include <utility>
#include <vector>

#include "core/INode.h"
#include "core/Patch.h"

namespace
{
   // One parameter of each saved type.
   class ParamNode : public INode
   {
   public:
      float f = 0.5f;
      int i = 3;
      bool b = true;
      std::string s = "plain";
      float c[3] = { 0.1f, 0.2f, 0.3f };

      unsigned int GetOutputTexture() override { return 0; }
      int GetOutputWidth() const override { return 0; }
      int GetOutputHeight() const override { return 0; }
      void CookIfNeeded(int) override {}
      void VisitParams(ParamVisitor& v) override
      {
         v.Float("f", f);
         v.Int("i", i);
         v.Bool("b", b);
         v.Text("s", s);
         v.Color("c", c);
      }
   };

   Patch::Data RoundTrip(const Patch::Data& in)
   {
      std::string text, err;
      REQUIRE(Patch::WriteText(in, text, err));
      Patch::Data out;
      REQUIRE(Patch::ReadText(text, out, err));
      return out;
   }

   Patch::NodeRecord Node(int index, const char* category, const char* type)
   {
      Patch::NodeRecord n;
      n.index = index;
      n.category = category;
      n.typeName = type;
      return n;
   }
}

TEST_CASE("params: every saved type round-trips, text keeps newlines and backslashes")
{
   ParamNode a;
   a.f = 1.25f;
   a.i = -7;
   a.b = false;
   a.s = "line one\nline two \\ end";
   a.c[0] = 0.75f;
   a.c[1] = 0.0f;
   a.c[2] = 1.0f;

   std::vector<std::pair<std::string, std::string>> saved;
   Patch::SaveParams(&a, saved);
   REQUIRE(saved.size() == 5);
   CHECK(saved[0].first == "f f");
   CHECK(saved[1].first == "i i");
   CHECK(saved[2].first == "b b");
   CHECK(saved[3].first == "s s");
   CHECK(saved[4].first == "c c");
   CHECK(saved[3].second.find('\n') == std::string::npos); // one parameter per line

   // Through the file text as well, not only the in-memory pairs.
   Patch::Data d;
   d.nodes.push_back(Node(0, "Test", "Param Node"));
   d.nodes[0].params = saved;
   const Patch::Data back = RoundTrip(d);
   REQUIRE(back.nodes.size() == 1);

   ParamNode b;
   Patch::LoadParams(&b, back.nodes[0].params);
   CHECK(b.f == a.f);
   CHECK(b.i == a.i);
   CHECK(b.b == a.b);
   CHECK(b.s == a.s);
   CHECK(b.c[0] == a.c[0]);
   CHECK(b.c[1] == a.c[1]);
   CHECK(b.c[2] == a.c[2]);
}

TEST_CASE("params: a key the patch does not have keeps the node's default")
{
   ParamNode n; // f = 0.5
   Patch::LoadParams(&n, { { "i i", "9" } });
   CHECK(n.i == 9);
   CHECK(n.f == 0.5f);
}

TEST_CASE("nodes, cables and modulation survive write and read")
{
   Patch::Data d;
   Patch::NodeRecord noise = Node(1, "PointZ", "Noise PointZ"); // type names hold spaces
   noise.x = 10.5f;
   noise.y = -20.0f;
   noise.showParams = true;
   noise.bypassed = true;
   noise.colorTag = 3;
   d.nodes.push_back(Node(0, "Modulator", "LFO"));
   d.nodes.push_back(noise);

   Patch::CableRecord cable;
   cable.dstIndex = 1;
   cable.dstSlot = 0;
   cable.srcIndex = 0;
   cable.srcOutput = 2; // multi-output source: the 4th token
   d.cables.push_back(cable);

   Patch::ModRecord mod;
   mod.dstIndex = 1;
   mod.dstParam = 4;
   mod.saveKey = "amplitude"; // written by key, read back as dstKey
   mod.srcIndex = 0;
   mod.srcOutput = 0;
   mod.polarity = 1;
   mod.depth = 0.5f;
   mod.centre = 0.25f;
   mod.inMin = 0.1f;
   mod.inMax = 0.9f;
   mod.outMin = 0.2f;
   mod.outMax = 0.8f;
   d.modulation.push_back(mod);

   const Patch::Data back = RoundTrip(d);
   REQUIRE(back.nodes.size() == 2);
   CHECK(back.nodes[1].typeName == "Noise PointZ");
   CHECK(back.nodes[1].category == "PointZ");
   CHECK(back.nodes[1].x == 10.5f);
   CHECK(back.nodes[1].y == -20.0f);
   CHECK(back.nodes[1].showParams);
   CHECK(back.nodes[1].bypassed);
   CHECK(back.nodes[1].colorTag == 3);

   REQUIRE(back.cables.size() == 1);
   CHECK(back.cables[0].dstIndex == 1);
   CHECK(back.cables[0].srcIndex == 0);
   CHECK(back.cables[0].srcOutput == 2);

   REQUIRE(back.modulation.size() == 1);
   const Patch::ModRecord& m = back.modulation[0];
   CHECK(m.dstKey == "amplitude");
   CHECK(m.polarity == 1);
   CHECK(m.depth == 0.5f);
   CHECK(m.centre == 0.25f);
   CHECK(m.inMin == 0.1f);
   CHECK(m.inMax == 0.9f);
   CHECK(m.outMin == 0.2f);
   CHECK(m.outMax == 0.8f);
   CHECK_FALSE(m.hasRange); // a Turbo file: range mapper, not upstream lo/hi
}

TEST_CASE("an upstream mod line (no Turbo marker) is read as lo / hi in destination units")
{
   const std::string text = "infinite-patch 1\n"
                            "node 0 Modulator LFO\n"
                            "end\n"
                            "node 1 PointZ Noise PointZ\n"
                            "end\n"
                            "mod 1 3 0 0 0 1 0 2.5 7.5\n";
   Patch::Data d;
   std::string err;
   REQUIRE(Patch::ReadText(text, d, err));
   REQUIRE(d.modulation.size() == 1);
   CHECK(d.modulation[0].dstParam == 3);
   CHECK(d.modulation[0].hasRange);
   CHECK(d.modulation[0].lo == 2.5f);
   CHECK(d.modulation[0].hi == 7.5f);
}

TEST_CASE("a pre-0.60 Turbo mod line (four unit values, no marker) keeps the range mapper")
{
   const std::string text = "infinite-patch 1\n"
                            "node 0 Modulator LFO\n"
                            "end\n"
                            "mod 0 2 0 0 0 1 0 0 1 0.2 0.7\n";
   Patch::Data d;
   std::string err;
   REQUIRE(Patch::ReadText(text, d, err));
   REQUIRE(d.modulation.size() == 1);
   CHECK_FALSE(d.modulation[0].hasRange);
   CHECK(d.modulation[0].outMin == doctest::Approx(0.2f));
   CHECK(d.modulation[0].outMax == doctest::Approx(0.7f));
}

TEST_CASE("lines from a newer version are skipped, the rest still reads")
{
   const std::string text = "infinite-patch 1\n"
                            "node 0 Source Image Source\n"
                            "  s path C:/media/a b.png\n"
                            "end\n"
                            "futuretag 1 2 3\n"
                            "node 1 Effects Blur\n"
                            "  f radius 4\n"
                            "end\n"
                            "cable 1 0 0\n";
   Patch::Data d;
   std::string err;
   REQUIRE(Patch::ReadText(text, d, err));
   REQUIRE(d.nodes.size() == 2);
   CHECK(d.nodes[0].typeName == "Image Source");
   REQUIRE(d.nodes[0].params.size() == 1);
   CHECK(d.nodes[0].params[0].first == "s path");
   CHECK(d.nodes[0].params[0].second == "C:/media/a b.png"); // values keep their spaces
   CHECK(d.cables.size() == 1);
}

// docs/patch-format.md, C2: the param layer only sees the keys a node visits;
// the lines it does not read are kept by the graph (GraphNode::foreignParams,
// PatchIO) and checked end to end by the FOREIGNKEYTEST self-test.
TEST_CASE("C2: SaveParams writes only the keys the node visits (PatchIO keeps the rest)")
{
   ParamNode n;
   Patch::LoadParams(&n, { { "f f", "2" }, { "f fromUpstream", "1" } });
   CHECK(n.f == 2.0f);
   std::vector<std::pair<std::string, std::string>> saved;
   Patch::SaveParams(&n, saved);
   bool kept = false;
   for (const auto& kv : saved)
      kept = kept || kv.first == "f fromUpstream";
   CHECK_FALSE(kept);
}
