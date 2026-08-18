#include "Patch.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include "INode.h"

namespace Patch
{
namespace
{
   const char* kMagic = "infinite-patch";
   const int kVersion = 1;

   std::vector<std::string> sRecents;
   const size_t kMaxRecents = 10;

   std::string RecentsPath()
   {
      const char* home = getenv("HOME");
      if (home == nullptr)
         return std::string();
      return std::string(home) + "/Library/Application Support/Infinite.recents";
   }

   // Written with %.9g so a float survives the round trip exactly rather than
   // drifting a little every time a patch is opened and saved again.
   std::string FloatToString(float v)
   {
      char buf[40];
      snprintf(buf, sizeof(buf), "%.9g", (double)v);
      return buf;
   }

   // Same escaping as Writer::Text/Reader::Text below, factored out for the
   // "expr" record - free text on a single line, outside of any node.
   std::string EscapeLine(const std::string& value)
   {
      std::string clean;
      for (char c : value)
      {
         if (c == '\\')
            clean += "\\\\";
         else if (c == '\n')
            clean += "\\n";
         else if (c != '\r')
            clean += c;
      }
      return clean;
   }

   std::string UnescapeLine(const std::string& raw)
   {
      std::string out;
      for (size_t i = 0; i < raw.size(); i++)
      {
         if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'n')
         {
            out += '\n';
            i++;
         }
         else if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\')
         {
            out += '\\';
            i++;
         }
         else
         {
            out += raw[i];
         }
      }
      return out;
   }

   class Writer : public ParamVisitor
   {
   public:
      std::vector<std::pair<std::string, std::string>> out;

      void Float(const char* name, float& value) override
      {
         out.push_back({ std::string("f ") + name, FloatToString(value) });
      }
      void Int(const char* name, int& value) override
      {
         out.push_back({ std::string("i ") + name, std::to_string(value) });
      }
      void Bool(const char* name, bool& value) override
      {
         out.push_back({ std::string("b ") + name, value ? "1" : "0" });
      }
      void Text(const char* name, std::string& value) override
      {
         // A newline cannot go in raw - the format is one parameter per line,
         // so it would be read back as the start of the next parameter. It is
         // escaped rather than folded to a space because for a Comment the line
         // breaks are the content, and because this same round trip is what
         // undo/redo and copy/paste use: folding lost a comment's shape on the
         // next undo, not just on the next save. Backslash is escaped too, so
         // unescaping on load has exactly one reading.
         out.push_back({ std::string("s ") + name, EscapeLine(value) });
      }
      void Color(const char* name, float rgb[3]) override
      {
         out.push_back({ std::string("c ") + name,
                         FloatToString(rgb[0]) + " " + FloatToString(rgb[1]) + " " +
                            FloatToString(rgb[2]) });
      }
   };

   class Reader : public ParamVisitor
   {
   public:
      std::map<std::string, std::string> values;

      // Every setter leaves the field untouched when the key is absent, so a
      // patch written before a parameter existed still loads and that parameter
      // simply keeps its default.
      void Float(const char* name, float& value) override
      {
         auto it = values.find(std::string("f ") + name);
         if (it != values.end())
            value = (float)atof(it->second.c_str());
      }
      void Int(const char* name, int& value) override
      {
         auto it = values.find(std::string("i ") + name);
         if (it != values.end())
            value = atoi(it->second.c_str());
      }
      void Bool(const char* name, bool& value) override
      {
         auto it = values.find(std::string("b ") + name);
         if (it != values.end())
            value = it->second == "1";
      }
      void Text(const char* name, std::string& value) override
      {
         auto it = values.find(std::string("s ") + name);
         if (it == values.end())
            return;
         // Undoes Writer::Text. Any other escape is left exactly as written:
         // patches saved before text was escaped at all never contain "\\n" or
         // "\\\\", so they read back unchanged.
         value = UnescapeLine(it->second);
      }
      void Color(const char* name, float rgb[3]) override
      {
         auto it = values.find(std::string("c ") + name);
         if (it == values.end())
            return;
         std::istringstream in(it->second);
         float r = rgb[0], g = rgb[1], b = rgb[2];
         in >> r >> g >> b;
         rgb[0] = r; rgb[1] = g; rgb[2] = b;
      }
   };
}

void SaveParams(INode* node, std::vector<std::pair<std::string, std::string>>& out)
{
   if (node == nullptr)
      return;
   Writer writer;
   node->VisitParams(writer);
   out = writer.out;
}

void LoadParams(INode* node, const std::vector<std::pair<std::string, std::string>>& in)
{
   if (node == nullptr)
      return;
   Reader reader;
   for (const auto& entry : in)
      reader.values[entry.first] = entry.second;
   node->VisitParams(reader);
}

bool Write(const std::string& path, const Data& data, std::string& outError)
{
   std::ofstream file(path);
   if (!file)
   {
      outError = "could not open " + path + " for writing";
      return false;
   }

   file << kMagic << " " << kVersion << "\n";
   for (const NodeRecord& node : data.nodes)
   {
      const float px = (std::isfinite(node.x) && std::abs(node.x) <= 1e6f && node.x > -2e9f) ? node.x : 0.0f;
      const float py = (std::isfinite(node.y) && std::abs(node.y) <= 1e6f && node.y > -2e9f) ? node.y : 0.0f;
      file << "node " << node.index << " " << node.category << " " << node.typeName << "\n";
      file << "  pos " << FloatToString(px) << " " << FloatToString(py) << "\n";
      file << "  flags " << (node.showParams ? 1 : 0) << " " << (node.bypassed ? 1 : 0) << " "
           << (node.showMiniViewport ? 1 : 0) << " " << (node.showAdvancedParams ? 1 : 0) << "\n";
      for (const auto& p : node.params)
         file << "  " << p.first << " " << p.second << "\n";
      file << "end\n";
   }
   for (const CableRecord& c : data.cables)
      file << "cable " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << "\n";
   for (const CableRecord& c : data.geometry)
      file << "geo " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << "\n";
   for (const CableRecord& c : data.audio)
      file << "aud " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << "\n";
   for (const CableRecord& c : data.notes)
      file << "note " << c.dstIndex << " " << c.dstSlot << " " << c.srcIndex << " " << c.srcOutput << "\n";
   for (const ModRecord& m : data.modulation)
      file << "mod " << m.dstIndex << " " << m.dstParam << " "
           << m.srcIndex << " " << m.srcOutput << " "
           << m.polarity << " " << FloatToString(m.depth) << " " << FloatToString(m.centre) << "\n";
   for (const PaletteRecord& p : data.palette)
      file << "pal " << p.dstIndex << " " << p.dstColor << " "
           << p.srcIndex << " " << p.srcSwatch << "\n";
   for (const ExprRecord& e : data.expressions)
      file << "expr " << e.dstIndex << " " << e.dstParam << " " << EscapeLine(e.text) << "\n";
   for (const GlobalRecord& g : data.globals)
      file << "glob " << g.name << " " << EscapeLine(g.expr) << "\n";

   if (!file.good())
   {
      outError = "write failed partway through " + path;
      return false;
   }
   return true;
}

bool Read(const std::string& path, Data& outData, std::string& outError)
{
   std::ifstream file(path);
   if (!file)
   {
      outError = "could not open " + path;
      return false;
   }

   std::string line;
   if (!std::getline(file, line))
   {
      outError = "file is empty";
      return false;
   }
   {
      std::istringstream header(line);
      std::string magic;
      int version = 0;
      header >> magic >> version;
      if (magic != kMagic)
      {
         outError = "not an Infinite patch";
         return false;
      }
      if (version > kVersion)
      {
         outError = "patch was written by a newer version of Infinite";
         return false;
      }
   }

   NodeRecord current;
   bool inNode = false;

   while (std::getline(file, line))
   {
      // Leading whitespace is cosmetic in the file, so strip it before parsing.
      size_t start = line.find_first_not_of(" \t");
      if (start == std::string::npos)
         continue;
      line = line.substr(start);

      std::istringstream in(line);
      std::string tag;
      in >> tag;

      if (tag == "node")
      {
         current = NodeRecord();
         in >> current.index >> current.category;
         std::getline(in, current.typeName);
         if (!current.typeName.empty() && current.typeName[0] == ' ')
            current.typeName.erase(0, 1);
         inNode = true;
      }
      else if (tag == "end")
      {
         if (inNode)
            outData.nodes.push_back(current);
         inNode = false;
      }
      else if (tag == "pos" && inNode)
      {
         in >> current.x >> current.y;
         if (!std::isfinite(current.x) || std::abs(current.x) > 1e6f || current.x <= -2e9f) current.x = 0.0f;
         if (!std::isfinite(current.y) || std::abs(current.y) > 1e6f || current.y <= -2e9f) current.y = 0.0f;
      }
      else if (tag == "flags" && inNode)
      {
         // advanced defaults to 0 (via >>'s C++11 failed-extraction behaviour)
         // when reading a patch written before showAdvancedParams existed -
         // `in` is a fresh istringstream over just this line (see the
         // getline loop above), so a missing 4th token cannot corrupt any
         // later line's parsing the way it would on a shared whole-file stream.
         int show = 0, bypass = 0, miniViewport = 0, advanced = 0;
         in >> show >> bypass >> miniViewport >> advanced;
         current.showParams = show != 0;
         current.bypassed = bypass != 0;
         current.showMiniViewport = miniViewport != 0;
         current.showAdvancedParams = advanced != 0;
      }
      else if (inNode && (tag == "f" || tag == "i" || tag == "b" || tag == "c" || tag == "s"))
      {
         std::string name;
         in >> name;
         std::string value;
         std::getline(in, value);
         if (!value.empty() && value[0] == ' ')
            value.erase(0, 1);
         current.params.push_back({ tag + " " + name, value });
      }
      else if (tag == "cable" || tag == "geo")
      {
         CableRecord c;
         in >> c.dstIndex >> c.dstSlot >> c.srcIndex;
         if (tag == "cable")
            outData.cables.push_back(c);
         else
            outData.geometry.push_back(c);
      }
      else if (tag == "aud" || tag == "note")
      {
         CableRecord c;
         in >> c.dstIndex >> c.dstSlot >> c.srcIndex;
         if (tag == "aud")
            outData.audio.push_back(c);
         else
         {
            // srcOutput is a later addition (Note Router); missing on older
            // patches, where >>'s failed-extraction behaviour leaves it 0 -
            // every note source but Router only ever has output 0 anyway.
            in >> c.srcOutput;
            outData.notes.push_back(c);
         }
      }
      else if (tag == "mod")
      {
         // polarity/depth/centre are a later addition; missing on older
         // patches, where >>'s failed-extraction behaviour leaves these
         // initialised values in place - see the "flags" precedent above.
         ModRecord m;
         m.polarity = 0;
         m.depth = 1.0f;
         m.centre = 0.0f;
         in >> m.dstIndex >> m.dstParam >> m.srcIndex >> m.srcOutput >> m.polarity >> m.depth >> m.centre;
         outData.modulation.push_back(m);
      }
      else if (tag == "pal")
      {
         PaletteRecord p;
         in >> p.dstIndex >> p.dstColor >> p.srcIndex >> p.srcSwatch;
         outData.palette.push_back(p);
      }
      else if (tag == "expr")
      {
         ExprRecord e;
         in >> e.dstIndex >> e.dstParam;
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         e.text = UnescapeLine(raw);
         outData.expressions.push_back(e);
      }
      else if (tag == "glob")
      {
         GlobalRecord g;
         in >> g.name;
         std::string raw;
         std::getline(in, raw);
         if (!raw.empty() && raw[0] == ' ')
            raw.erase(0, 1);
         g.expr = UnescapeLine(raw);
         if (!g.name.empty())
            outData.globals.push_back(g);
      }
      // Anything else is from a newer version and is deliberately ignored.
   }

   if (outData.nodes.empty())
   {
      outError = "patch contains no nodes";
      return false;
   }
   return true;
}

const std::vector<std::string>& Recents() { return sRecents; }

void NoteRecent(const std::string& path)
{
   if (path.empty())
      return;
   // Moved to the front rather than appended, so reopening a patch does not
   // leave duplicates scattered through the list.
   sRecents.erase(std::remove(sRecents.begin(), sRecents.end(), path), sRecents.end());
   sRecents.insert(sRecents.begin(), path);
   if (sRecents.size() > kMaxRecents)
      sRecents.resize(kMaxRecents);
   SaveRecents();
}

void LoadRecents()
{
   sRecents.clear();
   const std::string path = RecentsPath();
   if (path.empty())
      return;
   std::ifstream file(path);
   std::string line;
   while (std::getline(file, line) && sRecents.size() < kMaxRecents)
   {
      if (!line.empty())
         sRecents.push_back(line);
   }
}

void SaveRecents()
{
   const std::string path = RecentsPath();
   if (path.empty())
      return;
   std::ofstream file(path);
   for (const std::string& entry : sRecents)
      file << entry << "\n";
}
}
