#include "PatchGrammar.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

namespace chat
{
   namespace
   {
      using json = nlohmann::json;

      enum class TokKind { Word, Ref, Number, Equals, Formula, Agent };

      struct Tok
      {
         TokKind kind = TokKind::Word;
         size_t b = 0, e = 0;
         std::string text;  // Word: lower case; Formula: as written; Ref: what followed '@'
         int node = -1;     // Ref: resolved node, -1 when none or ambiguous
         std::vector<int> hits;
         int pin = -1;
         double number = 0.0;
         std::string numberText;  // the number as written, without its unit
      };

      bool WordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

      const std::set<std::string> kUnits = { "hz", "khz", "ms", "s", "db", "%", "bpm", "beats", "beat", "deg", "px" };

      // "0.5", "440hz", "-3db": the number and the unit it carries, if the whole word is that.
      bool ParseNumber(const std::string& word, double& value, std::string& numberText)
      {
         if (word.empty())
            return false;
         const char* start = word.c_str();
         char* end = nullptr;
         value = std::strtod(start, &end);
         if (end == start)
            return false;
         const std::string unit = Lower(std::string(end));
         if (!unit.empty() && kUnits.count(unit) == 0)
            return false;
         numberText = word.substr(0, (size_t)(end - start));
         return std::isfinite(value);
      }

      // Every `@` reference finds the longest node name written there: title#index, title, type or index.
      Tok LexRef(const std::string& line, const std::string& lower, size_t at, const Catalog& catalog)
      {
         Tok t;
         t.kind = TokKind::Ref;
         t.b = at;
         const size_t from = at + 1;
         // `@agent` is the default agent; the others go by what people call them (`@gemini`, `@chatgpt`)
         // or by their tool's name (`@antigravity`, `@codex`). The span keeps the tool's id.
         static const std::pair<const char*, const char*> kNames[] = {
            { "agent", "agent" },         { "claude", "claude" },  { "chatgpt", "codex" }, { "codex", "codex" },
            { "openai", "codex" },        { "gpt", "codex" },      { "gemini", "antigravity" },
            { "antigravity", "antigravity" }, { "agy", "antigravity" } };
         for (const auto& [name, id] : kNames)
         {
            const size_t n = std::strlen(name);
            if (lower.compare(from, n, name) == 0 && (from + n >= lower.size() || !WordChar(lower[from + n])))
            {
               t.kind = TokKind::Agent;
               t.e = from + n;
               t.text = id;
               return t;
            }
         }
         size_t best = 0;
         for (const CatalogNode& n : catalog.nodes)
         {
            const std::string idx = std::to_string(n.index);
            const std::string names[] = { Lower(n.title) + "#" + idx, Lower(n.title), Lower(n.type), idx };
            for (int k = 0; k < 4; k++)
            {
               const std::string& name = names[k];
               if (name.empty() || name == "#" + idx || lower.compare(from, name.size(), name) != 0)
                  continue;
               const size_t after = from + name.size();
               if (after < lower.size() && (WordChar(lower[after]) || (k != 0 && lower[after] == '#')))
                  continue;
               if (name.size() > best)
               {
                  best = name.size();
                  t.hits.clear();
               }
               if (name.size() == best && std::find(t.hits.begin(), t.hits.end(), n.index) == t.hits.end())
                  t.hits.push_back(n.index);
               // A title shadows a type of the same spelling, as in `infinite`.
               if (k <= 1)
                  break;
            }
         }
         if (best > 0)
         {
            // Titles win over types: drop type-only hits when a title also matched.
            std::vector<int> byTitle;
            for (int index : t.hits)
            {
               const CatalogNode* n = catalog.Node(index);
               const std::string title = Lower(n->title);
               if (lower.compare(from, best, title) == 0 || lower.compare(from, best, title + "#" + std::to_string(index)) == 0)
                  byTitle.push_back(index);
            }
            if (!byTitle.empty())
               t.hits = byTitle;
            t.e = from + best;
            t.text = line.substr(from, best);
            if (t.hits.size() == 1)
               t.node = t.hits[0];
         }
         else
         {
            size_t end = from;
            while (end < line.size() && !std::isspace((unsigned char)line[end]) && line[end] != ':' && line[end] != '.')
               end++;
            t.e = end;
            t.text = line.substr(from, end - from);
         }
         if (t.e < line.size() && line[t.e] == ':' && t.e + 1 < line.size() && std::isdigit((unsigned char)line[t.e + 1]))
         {
            size_t end = t.e + 1;
            while (end < line.size() && std::isdigit((unsigned char)line[end]))
               end++;
            t.pin = std::atoi(line.substr(t.e + 1, end - t.e - 1).c_str());
            t.e = end;
         }
         return t;
      }

      std::vector<Tok> Lex(const std::string& line, const Catalog& catalog)
      {
         const std::string lower = Lower(line);
         std::vector<Tok> toks;
         size_t i = 0;
         while (i < line.size())
         {
            const char c = line[i];
            if (std::isspace((unsigned char)c))
            {
               i++;
               continue;
            }
            if (c == '@')
            {
               toks.push_back(LexRef(line, lower, i, catalog));
               i = std::max(toks.back().e, i + 1);
               continue;
            }
            if (c == '.' && !toks.empty() && toks.back().kind == TokKind::Ref && toks.back().e == i)
            {
               i++;  // `@cube.detail`: the key follows as its own word
               continue;
            }
            if (c == '=')
            {
               Tok eq;
               eq.kind = TokKind::Equals;
               eq.b = i;
               eq.e = i + 1;
               toks.push_back(eq);
               // The formula runs to the next ` and ` or the end of the line.
               size_t start = i + 1;
               while (start < line.size() && std::isspace((unsigned char)line[start]))
                  start++;
               size_t end = lower.find(" and ", start);
               if (end == std::string::npos)
                  end = line.size();
               size_t last = end;
               while (last > start && std::isspace((unsigned char)line[last - 1]))
                  last--;
               if (last > start)
               {
                  Tok f;
                  f.kind = TokKind::Formula;
                  f.b = start;
                  f.e = last;
                  f.text = line.substr(start, last - start);
                  if (f.text.size() >= 2 && (f.text.front() == '"' || f.text.front() == '\'') && f.text.back() == f.text.front())
                     f.text = f.text.substr(1, f.text.size() - 2);
                  toks.push_back(f);
               }
               i = end;
               continue;
            }
            size_t end = i;
            while (end < line.size() && !std::isspace((unsigned char)line[end]) && line[end] != '=' && line[end] != '@')
               end++;
            Tok w;
            w.b = i;
            w.e = end;
            w.text = lower.substr(i, end - i);
            if (ParseNumber(line.substr(i, end - i), w.number, w.numberText))
               w.kind = TokKind::Number;
            toks.push_back(w);
            i = end;
         }
         // A unit written apart from its number ("440 Hz") joins the number.
         std::vector<Tok> out;
         for (const Tok& t : toks)
         {
            if (t.kind == TokKind::Word && kUnits.count(t.text) && !out.empty() && out.back().kind == TokKind::Number)
            {
               out.back().e = t.e;
               continue;
            }
            out.push_back(t);
         }
         return out;
      }

      int Distance(const std::string& a, const std::string& b)
      {
         std::vector<int> row(b.size() + 1);
         for (size_t j = 0; j <= b.size(); j++)
            row[j] = (int)j;
         for (size_t i = 1; i <= a.size(); i++)
         {
            int diag = row[0];
            row[0] = (int)i;
            for (size_t j = 1; j <= b.size(); j++)
            {
               const int up = row[j];
               row[j] = std::min({ row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] == b[j - 1] ? 0 : 1) });
               diag = up;
            }
         }
         return row[b.size()];
      }

      // The closest few of `options` to `word`: same prefix first, then by edit distance.
      std::vector<std::string> Nearest(const std::string& word, const std::vector<std::string>& options, size_t count = 5)
      {
         std::vector<std::pair<int, std::string>> scored;
         const std::string w = Lower(word);
         for (const std::string& o : options)
         {
            const std::string l = Lower(o);
            const int d = l.compare(0, w.size(), w) == 0 ? 0 : Distance(w, l);
            if (d <= std::max(2, (int)w.size() / 2))
               scored.push_back({ d, o });
         }
         std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
         std::vector<std::string> out;
         for (const auto& s : scored)
            if (out.size() < count)
               out.push_back(s.second);
         return out;
      }

      std::string Quote(const std::string& s)
      {
         if (!s.empty() && s.find_first_of(" \t\"'$`\\") == std::string::npos)
            return s;
         std::string out = "\"";
         for (char c : s)
         {
            if (c == '"' || c == '\\' || c == '$' || c == '`')
               out += '\\';
            out += c;
         }
         return out + "\"";
      }

      std::string FormatNumber(double v)
      {
         char buf[32];
         std::snprintf(buf, sizeof buf, "%g", v);
         return buf;
      }

      class Parser
      {
      public:
         Parser(const std::vector<Tok>& toks, size_t count, const Catalog& catalog, Parse& out)
            : mToks(toks), mCount(count), mCat(catalog), mOut(out) {}

         // Parses tokens [0, count). Returns true when every clause parsed.
         bool Run()
         {
            Expect(Slot::Start, -1, "", { "add", "connect", "remove", "bypass", "unbypass", "undo", "redo" });
            bool ok = true;
            bool first = true;
            while (mPos < mCount)
            {
               if (!first)
               {
                  if (!IsWord("and"))
                     return Fail(mToks[mPos], "expected `and` between two changes", { "and" });
                  Mark(mToks[mPos++], SpanKind::Joiner);
                  Expect(Slot::Start, -1, "", { "add", "connect", "remove", "bypass", "unbypass" });
                  if (mPos >= mCount)
                     return false;
               }
               first = false;
               if (!Clause())
                  return false;
               Expect(Slot::Joiner, -1, "", { "and" });
            }
            return ok && !first;
         }

      private:
         const std::vector<Tok>& mToks;
         size_t mCount;
         const Catalog& mCat;
         Parse& mOut;
         size_t mPos = 0;

         bool AtEnd() const { return mPos >= mCount; }
         bool IsWord(const char* w) const { return !AtEnd() && mToks[mPos].kind == TokKind::Word && mToks[mPos].text == w; }

         void Expect(Slot slot, int node, const std::string& key, std::vector<std::string> words = {})
         {
            mOut.slot = slot;
            mOut.slotNode = node;
            mOut.slotKey = key;
            mOut.words = std::move(words);
         }

         void Mark(const Tok& t, SpanKind kind, int node = -1, const std::string& key = "")
         {
            Span s;
            s.begin = t.b;
            s.end = t.e;
            s.kind = kind;
            s.node = node;
            s.key = key;
            mOut.spans.push_back(s);
         }

         bool Fail(const Tok& t, const std::string& message, std::vector<std::string> near = {})
         {
            if (mOut.error.empty())
            {
               mOut.error = message;
               mOut.errorBegin = t.b;
               mOut.errorEnd = t.e;
               mOut.near = std::move(near);
            }
            Mark(t, SpanKind::Bad);
            return false;
         }

         std::vector<std::string> NodeNames() const
         {
            std::vector<std::string> names;
            for (const CatalogNode& n : mCat.nodes)
               names.push_back(n.title.empty() ? n.type : n.title);
            return names;
         }

         // A node reference; `slot` is what the dropdown offers here.
         bool NodeRef(int& node, int& pin, Slot slot)
         {
            Expect(slot, -1, "");
            if (AtEnd())
               return false;
            const Tok& t = mToks[mPos];
            if (t.kind != TokKind::Ref)
               return Fail(t, "expected a node: type @ to pick one");
            if (t.node < 0)
            {
               if (t.hits.size() > 1)
               {
                  std::vector<std::string> names;
                  for (int index : t.hits)
                     names.push_back(mCat.Node(index)->title + "#" + std::to_string(index));
                  return Fail(t, "@" + t.text + " matches " + std::to_string(t.hits.size()) + " nodes", names);
               }
               return Fail(t, "no node named @" + t.text, Nearest(t.text, NodeNames()));
            }
            node = t.node;
            pin = t.pin;
            Mark(t, SpanKind::Node, node);
            mPos++;
            return true;
         }

         // A param of `node`: its key, or its label of up to three words.
         bool Param(int node, const CatalogParam*& param, bool modulatableOnly)
         {
            Expect(Slot::Param, node, "");
            if (AtEnd())
               return false;
            const CatalogNode* n = mCat.Node(node);
            for (size_t words = std::min<size_t>(3, mCount - mPos); words >= 1; words--)
            {
               bool allWords = true;
               std::string joined;
               for (size_t k = 0; k < words; k++)
               {
                  const Tok& t = mToks[mPos + k];
                  allWords = allWords && (t.kind == TokKind::Word || (t.kind == TokKind::Number && words > 1));
                  joined += (k ? " " : "") + t.text;
               }
               if (!allWords)
                  continue;
               if (const CatalogParam* p = n->Find(joined))
               {
                  param = p;
                  Tok span = mToks[mPos];
                  span.e = mToks[mPos + words - 1].e;
                  Mark(span, SpanKind::Param, node, p->key);
                  mPos += words;
                  return true;
               }
            }
            std::vector<std::string> keys;
            for (const CatalogParam& p : n->params)
               if ((!modulatableOnly || p.modulatable) && !IsInternal(p))
                  keys.push_back(p.key);
            return Fail(mToks[mPos], "@" + (n->title.empty() ? n->type : n->title) + " has no param '" + mToks[mPos].text + "'",
                        Nearest(mToks[mPos].text, keys));
         }

         static std::string Range(const CatalogParam& p)
         {
            return FormatNumber(p.min) + "–" + FormatNumber(p.max);
         }

         bool InRange(const CatalogParam& p, double v) const
         {
            if (p.kind.empty() || p.kind == "enum" || p.kind == "bool")
               return true;
            const double slack = 1e-6 * std::max(1.0, std::fabs((double)p.max - p.min));
            return v >= p.min - slack && v <= p.max + slack;
         }

         // A value for `param`: a number, an option name, or on/off. `saved` is what set_param takes.
         bool Value(int node, const CatalogParam& param, std::string& saved)
         {
            Expect(Slot::Value, node, param.key);
            if (AtEnd())
               return false;
            const Tok& t = mToks[mPos];
            if (t.kind == TokKind::Number)
            {
               if (param.kind == "enum" && !param.options.empty())
               {
                  const int index = (int)t.number;
                  if (index != t.number || index < 0 || index >= (int)param.options.size())
                     return Fail(t, param.key + " is one of its options", param.options);
               }
               else if (!InRange(param, t.number))
                  return Fail(t, param.key + " runs " + Range(param), { Range(param) });
               saved = t.numberText;
               Mark(t, SpanKind::Value, node, param.key);
               mPos++;
               return true;
            }
            if (t.kind == TokKind::Word)
            {
               if (param.kind == "bool" || param.kind.empty())
               {
                  static const std::set<std::string> kOn = { "on", "true", "yes" }, kOff = { "off", "false", "no" };
                  if (kOn.count(t.text) || kOff.count(t.text))
                  {
                     saved = kOn.count(t.text) ? "1" : "0";
                     Mark(t, SpanKind::Value, node, param.key);
                     mPos++;
                     return true;
                  }
               }
               // Option names can be several words: take the longest that matches.
               for (size_t words = std::min<size_t>(4, mCount - mPos); words >= 1; words--)
               {
                  std::string joined;
                  for (size_t k = 0; k < words; k++)
                     joined += (k ? " " : "") + mToks[mPos + k].text;
                  for (size_t o = 0; o < param.options.size(); o++)
                     if (Lower(param.options[o]) == joined)
                     {
                        saved = std::to_string(o);
                        Tok span = t;
                        span.e = mToks[mPos + words - 1].e;
                        Mark(span, SpanKind::Value, node, param.key);
                        mPos += words;
                        return true;
                     }
               }
               if (!param.options.empty())
                  return Fail(t, "'" + t.text + "' is not a " + param.key, Nearest(t.text, param.options));
            }
            return Fail(t, "expected a value for " + param.key, param.kind == "enum" ? param.options : std::vector<std::string>{ Range(param) });
         }

         bool Word(const char* w, std::vector<std::string> fits)
         {
            Expect(Slot::Joiner, -1, "", fits);
            if (AtEnd())
               return false;
            if (!IsWord(w))
               return Fail(mToks[mPos], std::string("expected `") + w + "`", fits);
            Mark(mToks[mPos++], SpanKind::Joiner);
            return true;
         }

         void Call(const char* method, json params, std::string cli)
         {
            mOut.calls.push_back({ { "method", method }, { "params", std::move(params) } });
            mOut.cli.push_back("infinite " + cli);
         }

         std::string Ref(int node, int pin = -1) const
         {
            return mCat.CliRef(node) + (pin > 0 ? ":" + std::to_string(pin) : "");
         }

         bool Clause()
         {
            const Tok& head = mToks[mPos];
            if (head.kind == TokKind::Word)
            {
               const std::string verb = head.text;
               if (verb == "undo" || verb == "redo")
               {
                  Mark(head, SpanKind::Verb);
                  mPos++;
                  Call(verb.c_str(), json::object(), verb);
                  return true;
               }
               if (verb == "add")
                  return Add();
               if (verb == "connect" || verb == "wire")
               {
                  Mark(head, SpanKind::Verb);
                  mPos++;
                  int src = -1, pin = -1;
                  if (!NodeRef(src, pin, Slot::Node))
                     return false;
                  return ConnectTail(src, pin);
               }
               if (verb == "remove" || verb == "delete" || verb == "bypass" || verb == "unbypass")
               {
                  Mark(head, SpanKind::Verb);
                  mPos++;
                  int node = -1, pin = -1;
                  if (!NodeRef(node, pin, Slot::Node))
                     return false;
                  if (verb == "remove" || verb == "delete")
                     Call("delete_node", { { "index", node } }, "remove " + Ref(node));
                  else
                     Call("set_bypass", { { "index", node }, { "bypassed", verb == "bypass" } },
                          "bypass " + Ref(node) + (verb == "bypass" ? " on" : " off"));
                  return true;
               }
               return Fail(head, "start with @node or a verb", Nearest(head.text, { "add", "connect", "remove", "bypass", "unbypass", "undo", "redo" }));
            }
            int node = -1, pin = -1;
            if (!NodeRef(node, pin, Slot::Node))
               return false;
            Expect(Slot::AfterNode, node, "", { "modulate", "set", "to" });
            if (AtEnd())
               return false;
            if (IsWord("to"))
               return ConnectTail(node, pin);
            if (IsWord("modulate") || IsWord("mod"))
            {
               Mark(mToks[mPos++], SpanKind::Verb);
               return Modulate(node);
            }
            const bool setWord = IsWord("set");
            if (setWord)
               Mark(mToks[mPos++], SpanKind::Verb);
            const CatalogParam* param = nullptr;
            if (!Param(node, param, false))
               return false;
            Expect(Slot::Joiner, node, param->key, { "to", "=" });
            if (AtEnd())
               return false;
            if (mToks[mPos].kind == TokKind::Equals)
            {
               Mark(mToks[mPos++], SpanKind::Joiner);
               Expect(Slot::None, node, param->key);
               if (AtEnd() || mToks[mPos].kind != TokKind::Formula)
                  return AtEnd() ? false : Fail(mToks[mPos], "expected a formula after =");
               const Tok& f = mToks[mPos++];
               double number = 0.0;
               std::string text;
               if (ParseNumber(f.text, number, text))
               {
                  // `@a key = 0.5` is a plain set.
                  if (!InRange(*param, number))
                     return Fail(f, param->key + " runs " + Range(*param), { Range(*param) });
                  Mark(f, SpanKind::Value, node, param->key);
                  Call("set_param", { { "index", node }, { "name", param->key }, { "value", text } },
                       "set " + Ref(node) + " " + param->key + " " + text);
                  return true;
               }
               Mark(f, SpanKind::Formula, node, param->key);
               const std::string formula = f.text[0] == '=' ? f.text : "=" + f.text;
               Call("set_expr", { { "index", node }, { "key", param->key }, { "text", formula } },
                    "expr " + Ref(node) + " " + param->key + " " + Quote(formula));
               return true;
            }
            if (mToks[mPos].kind != TokKind::Number && !Word("to", { "to", "=" }))
               return false;  // `@a detail 0.5` needs no `to`
            std::string saved;
            if (!Value(node, *param, saved))
               return false;
            Call("set_param", { { "index", node }, { "name", param->key }, { "value", saved } },
                 "set " + Ref(node) + " " + param->key + " " + Quote(saved));
            return true;
         }

         bool ConnectTail(int src, int pin)
         {
            if (!Word("to", { "to" }))
               return false;
            int dst = -1, slot = -1;
            if (!NodeRef(dst, slot, Slot::Node))
               return false;
            Call("connect", { { "srcIndex", src }, { "srcOutput", std::max(0, pin) }, { "dstIndex", dst }, { "dstSlot", std::max(0, slot) } },
                 "connect " + Ref(src, pin) + " " + Ref(dst, slot));
            return true;
         }

         bool Modulate(int dst)
         {
            const CatalogParam* param = nullptr;
            if (!Param(dst, param, true))
               return false;
            Expect(Slot::Joiner, dst, param->key, { "with" });
            if (AtEnd())
               return false;
            if (!IsWord("with") && !IsWord("by"))
               return Fail(mToks[mPos], "expected `with`", { "with" });
            Mark(mToks[mPos++], SpanKind::Joiner);
            int src = -1, pin = -1;
            if (!NodeRef(src, pin, Slot::Source))
               return false;
            json params = { { "src", src }, { "output", std::max(0, pin) }, { "dst", dst }, { "key", param->key } };
            std::string cli = "mod " + Ref(src, pin) + " " + Ref(dst) + " " + param->key;
            Expect(Slot::Joiner, dst, param->key, { "from", "and" });
            if (IsWord("from"))
            {
               Mark(mToks[mPos++], SpanKind::Joiner);
               double lo = 0.0, hi = 0.0;
               if (!RangeEnd(dst, *param, lo) || !Word("to", { "to" }) || !RangeEnd(dst, *param, hi))
                  return false;
               params["lo"] = lo;
               params["hi"] = hi;
               cli += " " + FormatNumber(lo) + " " + FormatNumber(hi);
            }
            Call("bind_mod", params, cli);
            return true;
         }

         bool RangeEnd(int node, const CatalogParam& param, double& v)
         {
            Expect(Slot::Value, node, param.key);
            if (AtEnd())
               return false;
            const Tok& t = mToks[mPos];
            if (t.kind != TokKind::Number)
               return Fail(t, "expected a number", { Range(param) });
            if (!InRange(param, t.number))
               return Fail(t, param.key + " runs " + Range(param), { Range(param) });
            v = t.number;
            Mark(t, SpanKind::Value, node, param.key);
            mPos++;
            return true;
         }

         bool Add()
         {
            Mark(mToks[mPos++], SpanKind::Verb);
            Expect(Slot::Type, -1, "");
            if (AtEnd())
               return false;
            // Type names can be several words ("Field Pixel"): the longest that matches.
            const CatalogType* type = nullptr;
            size_t used = 0;
            for (size_t words = std::min<size_t>(4, mCount - mPos); words >= 1 && type == nullptr; words--)
            {
               std::string joined;
               for (size_t k = 0; k < words; k++)
                  joined += (k ? " " : "") + mToks[mPos + k].text;
               for (const CatalogType& ct : mCat.types)
                  if (Lower(ct.name) == joined)
                  {
                     type = &ct;
                     used = words;
                     break;
                  }
            }
            if (type == nullptr)
            {
               std::vector<std::string> names;
               for (const CatalogType& ct : mCat.types)
                  names.push_back(ct.name);
               return Fail(mToks[mPos], "no node type '" + mToks[mPos].text + "'", Nearest(mToks[mPos].text, names));
            }
            Tok span = mToks[mPos];
            span.e = mToks[mPos + used - 1].e;
            Mark(span, SpanKind::Type);
            mPos += used;
            const int created = (int)mOut.calls.size();
            Call("create_node", { { "typeName", type->name }, { "category", type->category } }, "add " + Quote(type->name));
            Expect(Slot::Joiner, -1, "", { "after", "and" });
            if (IsWord("after"))
            {
               Mark(mToks[mPos++], SpanKind::Joiner);
               int src = -1, pin = -1;
               if (!NodeRef(src, pin, Slot::Node))
                  return false;
               // The new node's index is only known once it exists: the batch fills "$N.index" in.
               Call("connect", { { "srcIndex", src }, { "srcOutput", std::max(0, pin) }, { "dstIndex", "$" + std::to_string(created) + ".index" }, { "dstSlot", 0 } },
                    "connect " + Ref(src, pin) + " <new>");
            }
            return true;
         }
      };
   }

   Parse ParseLine(const std::string& line, const Catalog& catalog)
   {
      Parse out;
      const std::vector<Tok> toks = Lex(line, catalog);
      for (const Tok& t : toks)
      {
         if (t.kind == TokKind::Agent)
         {
            out.toAgent = true;
            Span s;
            s.begin = t.b;
            s.end = t.e;
            s.kind = SpanKind::Agent;
            s.key = t.text;  // "agent", "claude", "codex" or "antigravity"
            out.spans.push_back(s);
         }
         if (t.kind == TokKind::Word && t.text == "or")
            out.toAgent = true;
      }

      // The slot: parse everything before the word being typed. A line that ends in a space has none.
      const bool typing = !line.empty() && !std::isspace((unsigned char)line.back()) && !toks.empty();
      out.partialBegin = line.size();  // nothing being typed: a pick is added at the end, never over the text
      {
         Parse probe;
         Parser(toks, typing ? toks.size() - 1 : toks.size(), catalog, probe).Run();
         out.slot = probe.slot;
         out.slotNode = probe.slotNode;
         out.slotKey = probe.slotKey;
         out.words = probe.words;
         if (typing)
         {
            const Tok& last = toks.back();
            out.partialBegin = last.b;
            out.partial = line.substr(last.b, last.e - last.b);
            if (last.kind == TokKind::Formula || last.kind == TokKind::Equals)
               out.slot = Slot::None;
            else if (!out.partial.empty() && out.partial[0] == '@' && out.slot != Slot::Source)
               out.slot = Slot::Node;
         }
      }

      // Node chips show on every line, direct or not.
      auto addRefSpans = [&]() {
         for (const Tok& t : toks)
         {
            if (t.kind != TokKind::Ref || t.node < 0)
               continue;
            bool covered = false;
            for (const Span& s : out.spans)
               covered = covered || (s.begin == t.b && s.kind == SpanKind::Node);
            if (!covered)
            {
               Span s;
               s.begin = t.b;
               s.end = t.e;
               s.kind = SpanKind::Node;
               s.node = t.node;
               out.spans.push_back(s);
            }
         }
         std::sort(out.spans.begin(), out.spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
      };
      if (out.toAgent)
      {
         addRefSpans();
         return out;
      }
      Parse full;
      const bool ok = Parser(toks, toks.size(), catalog, full).Run();
      out.spans = full.spans;
      out.error = full.error;
      out.errorBegin = full.errorBegin;
      out.errorEnd = full.errorEnd;
      out.near = full.near;
      if (ok)
      {
         out.direct = true;
         out.calls = full.calls;
         out.cli = full.cli;
         // undo and redo cannot sit inside a batch: only alone.
         for (const json& c : out.calls)
            if ((c["method"] == "undo" || c["method"] == "redo") && out.calls.size() > 1)
            {
               out.direct = false;
               out.error = "undo and redo go on their own line";
            }
      }
      addRefSpans();
      return out;
   }
}
