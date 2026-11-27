// Photoshop's Type menu commands on text layers: OpenType toggles, Numerals, Stylistic Sets, kerning of a range and
// of a pair, size keys, relative steps, Insert Glyph, Paste Lorem Ipsum, missing fonts, default type styles and
// Check Spelling. The command shapes follow PhotoCraft's crates/engine/src/cmd/type_extra_cmds.rs,
// type_spell_cmds.rs and type_cmds.rs (Apache License 2.0, Copyright the PhotoCraft authors).
#include "Scene2DNode.h"

#include "platform/AppPaths.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace
{
   using Node = Scene2DNode;
   constexpr size_t kAll = std::string::npos;

   bool Fail(std::string* err, const std::string& why)
   {
      if (err != nullptr)
         *err = why;
      std::fprintf(stderr, "[type] %s\n", why.c_str());
      return false;
   }
   bool IsCont(unsigned char c) { return (c & 0xC0) == 0x80; }
   size_t Snap(const std::string& s, size_t at)
   {
      at = std::min(at, s.size());
      while (at > 0 && at < s.size() && IsCont((unsigned char)s[at]))
         at--;
      return at;
   }
   // The range in bytes, read in order and clamped; npos .. npos = the whole text.
   void Range(const std::string& t, size_t& a, size_t& b)
   {
      if (a == kAll && b == kAll)
      {
         a = 0;
         b = t.size();
         return;
      }
      if (a > b)
         std::swap(a, b);
      a = Snap(t, a);
      b = Snap(t, b);
   }
   std::vector<std::string> Tags(const std::string& f)
   {
      std::vector<std::string> v;
      std::istringstream in(f);
      std::string t;
      while (in >> t)
         v.push_back(t);
      return v;
   }
   std::string Join(std::vector<std::string> v)
   {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
      std::string o;
      for (const std::string& t : v)
         o += (o.empty() ? "" : " ") + t;
      return o;
   }
   bool ValidTag(const std::string& tag)
   {
      static const char* known[] = { "liga", "dlig", "frac", "swsh", "salt", "ordn", "titl", "sups", "sinf", "calt", "smcp",
                                     "c2sc", "zero", "case", "hlig" };
      for (const char* k : known)
         if (tag == k)
            return true;
      if (tag.size() == 4 && tag[0] == 's' && tag[1] == 's' && std::isdigit((unsigned char)tag[2]) && std::isdigit((unsigned char)tag[3]))
      {
         const int n = (tag[2] - '0') * 10 + (tag[3] - '0');
         return n >= 1 && n <= 20;
      }
      return false;
   }
   const char* kFigureTags[4] = { "lnum", "onum", "pnum", "tnum" };
   std::string LowerAscii(std::string s)
   {
      for (char& c : s)
         c = (char)std::tolower((unsigned char)c);
      return s;
   }
   bool WordByte(unsigned char c) { return std::isalnum(c) || c == '\'' || c >= 0x80; }
}

bool Scene2DNode::FeatureOn(const RunStyle& s, const std::string& tag)
{
   for (const std::string& t : Tags(s.features))
      if (t == tag)
         return true;
   if (tag == "liga")
      return !FeatureOn(s, "-liga");
   return false;
}

void Scene2DNode::SetFeature(RunStyle& s, const std::string& tag, bool on)
{
   std::vector<std::string> v;
   for (const std::string& t : Tags(s.features))
      if (t != tag && t != "-" + tag)
         v.push_back(t);
   if (tag == "liga")
   {
      if (!on)
         v.push_back("-liga");
   }
   else if (on)
      v.push_back(tag);
   s.features = Join(v);
}

bool Scene2DNode::SetTypeFeature(const std::vector<int>& layers, const std::string& tag, int on, size_t a, size_t b, bool* outOn,
                                 std::string* err)
{
   if (!ValidTag(tag))
      return Fail(err, "openType: unknown feature " + tag);
   if (layers.empty())
      return Fail(err, "openType: no type layer");
   for (int id : layers)
   {
      Layer* l = FindLayer(id);
      if (l == nullptr)
         return Fail(err, "openType: no layer " + std::to_string(id));
      if (l->kind != Kind::Text)
         return Fail(err, "openType: not a type layer");
      if (EffectiveLock(*l) & kLockAll)
         return Fail(err, "openType: the layer is locked");
   }
   // Toggle: the direction is the opposite of the first character of the first layer's range.
   bool set = on > 0;
   if (on < 0)
   {
      Layer* l = FindLayer(layers[0]);
      SyncTextRuns(*l);
      size_t ra = a, rb = b;
      Range(l->text, ra, rb);
      const size_t at = ra < l->text.size() ? ra : (l->text.empty() ? 0 : l->text.size() - 1);
      set = !FeatureOn(l->runs[RunAt(l->runs, at)].st, tag);
   }
   for (int id : layers)
   {
      Layer* l = FindLayer(id);
      size_t ra = a, rb = b;
      SyncTextRuns(*l);
      Range(l->text, ra, rb);
      if (ra == rb && !l->text.empty())
         continue;
      StyleRange(*l, ra, rb, [&](RunStyle& s) { SetFeature(s, tag, set); });
   }
   if (outOn != nullptr)
      *outOn = set;
   std::fprintf(stderr, "PROBE type feature %s %s on %zu layer(s)\n", tag.c_str(), set ? "on" : "off", layers.size());
   Commit();
   return true;
}

const char* Scene2DNode::NumeralsLabel(Numerals n)
{
   switch (n)
   {
   case Numerals::Default: return "Default Figures";
   case Numerals::Lining: return "Lining Figures";
   case Numerals::Oldstyle: return "Oldstyle Figures";
   case Numerals::Proportional: return "Proportional Figures";
   case Numerals::Tabular: return "Tabular Figures";
   case Numerals::ProportionalLining: return "Proportional Lining";
   case Numerals::TabularLining: return "Tabular Lining";
   case Numerals::ProportionalOldstyle: return "Proportional Oldstyle";
   case Numerals::TabularOldstyle: return "Tabular Oldstyle";
   default: return "";
   }
}

Scene2DNode::Numerals Scene2DNode::NumeralsOf(const RunStyle& s)
{
   const bool ln = FeatureOn(s, "lnum"), on = FeatureOn(s, "onum"), pn = FeatureOn(s, "pnum"), tn = FeatureOn(s, "tnum");
   if (tn && ln) return Numerals::TabularLining;
   if (pn && ln) return Numerals::ProportionalLining;
   if (tn && on) return Numerals::TabularOldstyle;
   if (pn && on) return Numerals::ProportionalOldstyle;
   if (ln) return Numerals::Lining;
   if (on) return Numerals::Oldstyle;
   if (pn) return Numerals::Proportional;
   if (tn) return Numerals::Tabular;
   return Numerals::Default;
}

bool Scene2DNode::SetNumerals(const std::vector<int>& layers, Numerals n, size_t a, size_t b, std::string* err)
{
   if ((int)n < 0 || n >= Numerals::Count)
      return Fail(err, "numerals: unknown style");
   if (layers.empty())
      return Fail(err, "numerals: no type layer");
   for (int id : layers)
   {
      Layer* l = FindLayer(id);
      if (l == nullptr)
         return Fail(err, "numerals: no layer " + std::to_string(id));
      if (l->kind != Kind::Text)
         return Fail(err, "numerals: not a type layer");
      if (EffectiveLock(*l) & kLockAll)
         return Fail(err, "numerals: the layer is locked");
   }
   std::vector<const char*> want;
   switch (n)
   {
   case Numerals::Lining: want = { "lnum" }; break;
   case Numerals::Oldstyle: want = { "onum" }; break;
   case Numerals::Proportional: want = { "pnum" }; break;
   case Numerals::Tabular: want = { "tnum" }; break;
   case Numerals::ProportionalLining: want = { "pnum", "lnum" }; break;
   case Numerals::TabularLining: want = { "tnum", "lnum" }; break;
   case Numerals::ProportionalOldstyle: want = { "pnum", "onum" }; break;
   case Numerals::TabularOldstyle: want = { "tnum", "onum" }; break;
   default: break;
   }
   for (int id : layers)
   {
      Layer* l = FindLayer(id);
      size_t ra = a, rb = b;
      SyncTextRuns(*l);
      Range(l->text, ra, rb);
      if (ra == rb && !l->text.empty())
         continue;
      StyleRange(*l, ra, rb, [&](RunStyle& s)
                 {
                    for (const char* t : kFigureTags)
                       SetFeature(s, t, false);
                    for (const char* t : want)
                       SetFeature(s, t, true);
                 });
   }
   std::fprintf(stderr, "PROBE type numerals %s on %zu layer(s)\n", NumeralsLabel(n), layers.size());
   Commit();
   return true;
}

bool Scene2DNode::SetRangeKerning(int layer, size_t a, size_t b, int mode, float value, std::string* err)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "kerning: not a type layer");
   if (EffectiveLock(*l) & kLockAll)
      return Fail(err, "kerning: the layer is locked");
   if (mode < 0 || mode > 2 || !std::isfinite(value))
      return Fail(err, "kerning: mode is metrics, optical or a number");
   SyncTextRuns(*l);
   Range(l->text, a, b);
   const float v = mode == 2 ? std::clamp(value, -1000.0f, 10000.0f) : 0.0f;
   StyleRange(*l, a, b, [&](RunStyle& s) { s.kernMode = mode; s.kern = v; });
   std::fprintf(stderr, "PROBE type kerning mode %d value %g\n", mode, v);
   Commit();
   return true;
}

bool Scene2DNode::KernPair(int layer, size_t at, float by, std::string* err)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "kernPair: not a type layer");
   if (EffectiveLock(*l) & kLockAll)
      return Fail(err, "kernPair: the layer is locked");
   if (!std::isfinite(by))
      return Fail(err, "kernPair: the step must be a number");
   SyncTextRuns(*l);
   at = Snap(l->text, at);
   if (at == 0 || at >= l->text.size())
      return Fail(err, "kernPair: the caret is not between two characters");
   size_t p = at - 1;
   while (p > 0 && IsCont((unsigned char)l->text[p]))
      p--;
   size_t q = at + 1;
   while (q < l->text.size() && IsCont((unsigned char)l->text[q]))
      q++;
   const RunStyle st = l->runs[RunAt(l->runs, p)].st;
   // What shows now: the fixed kern when off, else the font's pair (1/1000 em, rounded).
   float shown = st.kern;
   if (st.kernMode != 2)
   {
      const float size = std::max(1.0f, st.size);
      shown = std::round(Platform::PairKerning(st.font, size, l->text.substr(p, at - p), l->text.substr(at, q - at)) / size * 1000.0f);
   }
   const float v = std::clamp(shown + by, -1000.0f, 10000.0f);
   StyleRange(*l, p, at, [&](RunStyle& s) { s.kernMode = 2; s.kern = v; });
   std::fprintf(stderr, "PROBE type kern pair at %zu: %g -> %g\n", at, shown, v);
   Commit();
   return true;
}

bool Scene2DNode::StepTypeSize(int layer, size_t a, size_t b, float by, std::string* err)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "size: not a type layer");
   if (EffectiveLock(*l) & kLockAll)
      return Fail(err, "size: the layer is locked");
   if (!std::isfinite(by))
      by = by > 0 ? 2000.0f : -2000.0f;
   by = std::clamp(by, -4000.0f, 4000.0f);
   const float scale = std::max(1e-3f, std::sqrt(std::fabs(l->sx * l->sy)));
   SyncTextRuns(*l);
   Range(l->text, a, b);
   if (a == b && !l->text.empty())
      return true;
   StyleRange(*l, a, b, [&](RunStyle& s) { s.size = std::clamp((s.size * scale + by) / scale, 1.0f, 2000.0f); });
   std::fprintf(stderr, "PROBE type size step %g (layer scale %g)\n", by, scale);
   Commit();
   return true;
}

bool Scene2DNode::StepTypeStyle(int layer, size_t a, size_t b, float trackingBy, float leadingBy, float shiftBy, std::string* err)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "step: not a type layer");
   if (EffectiveLock(*l) & kLockAll)
      return Fail(err, "step: the layer is locked");
   for (float f : { trackingBy, leadingBy, shiftBy })
      if (!std::isfinite(f) || std::fabs(f) > 1e6f)
         return Fail(err, "step: steps must be finite numbers");
   SyncTextRuns(*l);
   Range(l->text, a, b);
   if (a == b && !l->text.empty())
      return true;
   if (trackingBy != 0.0f)
      StyleRange(*l, a, b, [&](RunStyle& s) { s.tracking = std::clamp(s.tracking + trackingBy, -1000.0f, 1000.0f); });
   // Leading is a multiple of the size here: a px step moves it by step / size.
   if (leadingBy != 0.0f)
      l->leading = std::clamp(l->leading + leadingBy / std::max(1.0f, l->fontSize), 0.0f, 100.0f);
   if (shiftBy != 0.0f)
      l->baseShift = std::clamp(l->baseShift + shiftBy, -10000.0f, 10000.0f);
   std::fprintf(stderr, "PROBE type step tracking %g leading %g shift %g\n", trackingBy, leadingBy, shiftBy);
   Commit();
   return true;
}

bool Scene2DNode::InsertTypeText(int layer, size_t a, size_t b, const std::string& t, size_t* outCaret, std::string* err)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "insertText: not a type layer");
   if (EffectiveLock(*l) & kLockAll)
      return Fail(err, "insertText: the layer is locked");
   std::string ins;
   for (size_t i = 0; i < t.size(); i++)
   {
      if (t[i] == '\r')
      {
         ins += '\n';
         if (i + 1 < t.size() && t[i + 1] == '\n')
            i++;
      }
      else
         ins += t[i];
   }
   if (ins.empty())
      return Fail(err, "insertText: nothing to insert");
   SyncTextRuns(*l);
   if (a == kAll)
      a = b = l->text.size();
   if (b == kAll)
      b = a;
   if (a > b)
      std::swap(a, b);
   a = Snap(l->text, a);
   b = Snap(l->text, b);
   ReplaceRunText(*l, a, b, ins);
   if (outCaret != nullptr)
      *outCaret = a + ins.size();
   std::fprintf(stderr, "PROBE type insert %zu bytes at %zu\n", ins.size(), a);
   NoteTypeWarnings(layer);
   Commit();
   return true;
}

const char* Scene2DNode::LoremIpsum()
{
   return "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore "
          "magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo "
          "consequat.";
}

int Scene2DNode::PasteLoremIpsum(int layer, size_t at, size_t* outCaret, std::string* err)
{
   if (layer == 0)
   {
      const float w = std::max(16.0f, mDoc.w * 0.6f);
      const int id = AddTextLayer(LoremIpsum(), mDoc.w * 0.2f, mDoc.h * 0.2f, w);
      if (outCaret != nullptr)
         *outCaret = std::string(LoremIpsum()).size();
      std::fprintf(stderr, "PROBE type lorem new box %d\n", id);
      return id;
   }
   size_t caret = 0;
   if (!InsertTypeText(layer, at, at, LoremIpsum(), &caret, err))
      return -1;
   if (outCaret != nullptr)
      *outCaret = caret;
   return layer;
}

std::string Scene2DNode::FontWarning(const std::string& family)
{
   if (family.empty())
      return {};
   for (const std::string& f : Platform::AvailableFontFamilies())
      if (f == family)
         return {};
   // A run may name an exact face by its PostScript name (as PSDs and agents do); an installed face is not missing even
   // when its family is called something else. PhotoCraft type_extra_cmds style_font_missing (Apache 2.0).
   if (Platform::FontFaceInstalled(family))
      return {};
   return "font '" + family + "' is not installed; text may render using a fallback face";
}

std::vector<std::string> Scene2DNode::TypeWarnings(int layer)
{
   std::vector<std::string> out;
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return out;
   std::set<std::string> seen;
   for (const TextRun& r : CharRuns(*l))
   {
      const std::string w = FontWarning(r.st.font);
      if (!w.empty() && seen.insert(r.st.font).second)
         out.push_back(w);
   }
   return out;
}

void Scene2DNode::NoteTypeWarnings(int layer)
{
   mTypeWarnings = TypeWarnings(layer);
   for (const std::string& w : mTypeWarnings)
      std::fprintf(stderr, "PROBE type warning layer %d: %s\n", layer, w.c_str());
}

std::vector<std::string> Scene2DNode::MissingFonts()
{
   std::set<std::string> miss;
   for (Layer& l : Layers())
   {
      if (l.kind != Kind::Text)
         continue;
      for (const TextRun& r : CharRuns(l))
         if (!FontWarning(r.st.font).empty())
            miss.insert(r.st.font);
   }
   return { miss.begin(), miss.end() };
}

int Scene2DNode::ReplaceMissingFonts(const std::map<std::string, std::string>& map, const std::string& fallback)
{
   int n = 0;
   for (Layer& l : Layers())
   {
      if (l.kind != Kind::Text || (EffectiveLock(l) & kLockAll))
         continue;
      SyncTextRuns(l);
      bool changed = false;
      for (TextRun& r : l.runs)
      {
         // A run whose font resolves keeps it (only missing faces are replaced).
         if (FontWarning(r.st.font).empty())
            continue;
         auto it = map.find(r.st.font);
         const std::string to = it != map.end() ? it->second : fallback;
         if (to.empty() || to == r.st.font)
            continue;
         r.st.font = to;
         changed = true;
      }
      if (changed)
      {
         NormalizeRuns(l);
         n++;
      }
   }
   std::fprintf(stderr, "PROBE type replaced missing fonts on %d layer(s)\n", n);
   if (n > 0)
      Commit();
   return n;
}

bool Scene2DNode::SaveDefaultTypeStyle(int layer)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return false;
   SyncTextRuns(*l);
   mDefaultType = l->runs[0].st;
   mDefaultType.charStyle = 0;
   mDefaultTypeSet = true;
   std::fprintf(stderr, "PROBE type default style saved: %s %g\n", mDefaultType.font.c_str(), mDefaultType.size);
   return true;
}

bool Scene2DNode::LoadDefaultTypeStyle(int layer)
{
   Layer* l = FindLayer(layer);
   if (!mDefaultTypeSet || l == nullptr || l->kind != Kind::Text || (EffectiveLock(*l) & kLockAll))
      return false;
   SyncTextRuns(*l);
   for (TextRun& r : l->runs)
      r.st = mDefaultType;
   NormalizeRuns(*l);
   Commit();
   return true;
}

int Scene2DNode::UpdateAllTextLayers()
{
   int n = 0;
   for (Layer& l : Layers())
      if (l.kind == Kind::Text)
      {
         SyncTextRuns(l);
         mTextBoxes.erase(l.id);
         n++;
      }
   std::fprintf(stderr, "PROBE type updated %d text layer(s)\n", n);
   if (n > 0)
      Commit();
   return n;
}

// ---- spelling -------------------------------------------------------------------

std::vector<std::string>& Scene2DNode::UserDictionary()
{
   static std::vector<std::string> words;
   static bool loaded = false;
   if (!loaded)
   {
      loaded = true;
      std::ifstream in(AppPaths::FsPath(AppPaths::SettingsDir() + "/user-dictionary.txt"));
      std::string w;
      while (std::getline(in, w))
         if (!w.empty())
            words.push_back(w);
   }
   return words;
}

namespace
{
   void SaveDictionary()
   {
      std::ofstream out(AppPaths::FsPath(AppPaths::SettingsDir() + "/user-dictionary.txt"));
      for (const std::string& w : Scene2DNode::UserDictionary())
         out << w << "\n";
   }
}

void Scene2DNode::AddToDictionary(const std::string& w)
{
   if (w.empty())
      return;
   std::vector<std::string>& d = UserDictionary();
   for (const std::string& x : d)
      if (LowerAscii(x) == LowerAscii(w))
         return;
   d.push_back(w);
   SaveDictionary();
}

void Scene2DNode::RemoveFromDictionary(const std::string& w)
{
   std::vector<std::string>& d = UserDictionary();
   const size_t n = d.size();
   d.erase(std::remove_if(d.begin(), d.end(), [&](const std::string& x) { return LowerAscii(x) == LowerAscii(w); }), d.end());
   if (d.size() != n)
      SaveDictionary();
}

bool Scene2DNode::HasTextLayer()
{
   for (Layer& l : Layers())
      if (l.kind == Kind::Text)
         return true;
   return false;
}

int Scene2DNode::TextLayerByName(const std::string& name)
{
   for (Layer& l : Layers())
      if (l.kind == Kind::Text && l.name == name)
         return l.id;
   return 0;
}

std::vector<size_t> Scene2DNode::CharOffsets(const std::string& text, const std::vector<size_t>& bytes)
{
   std::vector<size_t> out;
   out.reserve(bytes.size());
   for (size_t b : bytes)
   {
      size_t n = 0;
      const size_t end = std::min(b, text.size());
      for (size_t i = 0; i < end; i++)
         if (!IsCont((unsigned char)text[i]))
            n++;
      out.push_back(n);
   }
   return out;
}

std::vector<Scene2DNode::Misspelling> Scene2DNode::CheckSpelling(const std::vector<int>& layers, const std::set<std::string>& ignore,
                                                                 int suggestions)
{
   std::vector<Misspelling> out;
   std::vector<int> ids = layers;
   if (ids.empty())
      for (Layer& l : Layers())
         if (l.kind == Kind::Text)
            ids.push_back(l.id);
   std::set<std::string> skip;
   for (const std::string& w : ignore)
      skip.insert(LowerAscii(w));
   for (const std::string& w : UserDictionary())
      skip.insert(LowerAscii(w));
   for (int id : ids)
   {
      const Layer* l = FindLayer(id);
      if (l == nullptr || l->kind != Kind::Text)
         continue;
      const std::string& t = l->text;
      for (size_t i = 0; i < t.size();)
      {
         if (!WordByte((unsigned char)t[i]) || t[i] == '\'')
         {
            i++;
            continue;
         }
         size_t j = i;
         while (j < t.size() && WordByte((unsigned char)t[j]))
            j++;
         while (j > i && t[j - 1] == '\'')
            j--;
         const std::string w = t.substr(i, j - i);
         bool digits = true;
         for (char c : w)
            digits = digits && std::isdigit((unsigned char)c);
         if (!digits && !skip.count(LowerAscii(w)) && !Platform::CheckWord(w, l->lang))
         {
            Misspelling m;
            m.layer = id;
            m.start = i;
            m.end = j;
            m.word = w;
            m.suggestions = Platform::SpellSuggestions(w, suggestions, l->lang);
            out.push_back(std::move(m));
         }
         i = std::max(j, i + 1);
      }
   }
   std::fprintf(stderr, "PROBE spelling: %zu misspelled word(s) in %zu layer(s)\n", out.size(), ids.size());
   return out;
}

bool Scene2DNode::SpellChange(int layer, size_t start, size_t end, const std::string& word, const std::string& replace, std::string* err)
{
   Layer* l = FindLayer(layer);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "checkSpelling: not a type layer");
   if (EffectiveLock(*l) & kLockAll)
      return Fail(err, "checkSpelling: the layer is locked");
   if (start > end || end > l->text.size() || l->text.compare(start, end - start, word) != 0)
      return Fail(err, "checkSpelling: the word is no longer there");
   ReplaceRunText(*l, start, end, replace);
   std::fprintf(stderr, "PROBE spelling change %s -> %s\n", word.c_str(), replace.c_str());
   Commit();
   return true;
}

int Scene2DNode::SpellChangeAll(const std::vector<int>& layers, const std::string& word, const std::string& replace)
{
   if (word.empty())
      return 0;
   std::vector<int> ids = layers;
   if (ids.empty())
      for (Layer& l : Layers())
         if (l.kind == Kind::Text)
            ids.push_back(l.id);
   int n = 0;
   for (int id : ids)
   {
      Layer* l = FindLayer(id);
      if (l == nullptr || l->kind != Kind::Text || (EffectiveLock(*l) & kLockAll))
         continue;
      // Whole words only, exact case; from the end so earlier offsets stay put.
      std::vector<size_t> hits;
      for (size_t p = l->text.find(word); p != std::string::npos; p = l->text.find(word, p + 1))
      {
         const bool left = p == 0 || !WordByte((unsigned char)l->text[p - 1]);
         const size_t e = p + word.size();
         const bool right = e >= l->text.size() || !WordByte((unsigned char)l->text[e]);
         if (left && right)
            hits.push_back(p);
      }
      for (auto it = hits.rbegin(); it != hits.rend(); ++it)
         ReplaceRunText(*l, *it, *it + word.size(), replace);
      n += (int)hits.size();
   }
   std::fprintf(stderr, "PROBE spelling change all %s -> %s: %d\n", word.c_str(), replace.c_str(), n);
   if (n > 0)
      Commit();
   return n;
}

// ---- type on a path: placement, anchors, box, point and paragraph (PhotoCraft type_extra_cmds, Apache 2.0) ----------

double Scene2DNode::TypePathLength(const Layer& l)
{
   if (l.typePath.empty())
      return 0.0;
   std::vector<std::vector<float>> lines;
   Outline(l.typePath[0], lines);
   if (lines.empty() || lines[0].size() < 4)
      return 0.0;
   std::vector<float> P = lines[0];
   if (l.typePath[0].closed)
      P.insert(P.end(), { P[0], P[1] });
   double len = 0.0;
   for (size_t i = 2; i + 1 < P.size(); i += 2)
      len += std::hypot((double)P[i] - P[i - 2], (double)P[i + 1] - P[i - 1]);
   return len;
}

bool Scene2DNode::SetPathPlacement(int id, float start, float end, bool flip, std::string* err)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text || l->typeMode != 1)
      return Fail(err, "setPathPlacement: not type on a path");
   const double len = TypePathLength(*l);
   if (!(len > 0.0))
      return Fail(err, "setPathPlacement: the path has no length");
   // The float the length rounds to may be a hair past it: that value still means the end, normalised to the largest
   // float that is not past the path.
   const float lenF = (float)len;
   const float canonical = (double)lenF > len ? std::nextafter(lenF, 0.0f) : lenF;
   auto fit = [&](float v, float& out)
   {
      if (!std::isfinite(v) || v < 0.0f || v > lenF)
         return false;
      out = std::min(v, canonical);
      return true;
   };
   float s = 0.0f, e = -1.0f;
   if (!fit(start, s) || (end >= 0.0f && !fit(end, e)) || (end >= 0.0f && e < s))
      return Fail(err, "setPathPlacement: out of the path");
   l->pathOffset = (float)(s / len);
   l->pathEnd = end >= 0.0f ? (float)(e / len) : -1.0f;
   l->pathFlip = flip;
   std::fprintf(stderr, "PROBE path placement layer %d start %.9g end %.9g of %.9g flip %d\n", id, s, e, len, (int)flip);
   Commit();
   return true;
}

bool Scene2DNode::MovePathAnchor(int id, bool typePath, int sub, int anchor, float dx, float dy)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || !std::isfinite(dx) || !std::isfinite(dy))
      return false;
   std::vector<Shape>& subs = typePath ? l->typePath : l->vmask;
   if ((typePath && l->typeMode == 0) || (!typePath && !l->hasVMask) || sub < 0 || sub >= (int)subs.size())
      return false;
   Shape& sh = subs[sub];
   if (sh.type == Shape::kPath)
   {
      if (anchor < 0 || (size_t)anchor * 6 + 5 >= sh.pts.size())
         return false;
      sh.pts[anchor * 6] += dx;
      sh.pts[anchor * 6 + 1] += dy;
      sh.pts[anchor * 6 + 2] += dx;
      sh.pts[anchor * 6 + 3] += dy;
      sh.pts[anchor * 6 + 4] += dx;
      sh.pts[anchor * 6 + 5] += dy;
   }
   else if (anchor == 0)
   {
      sh.x0 += dx;
      sh.y0 += dy;
   }
   else if (anchor == 1)
   {
      sh.x1 += dx;
      sh.y1 += dy;
   }
   else
      return false;
   std::fprintf(stderr, "PROBE move %s anchor layer %d %d/%d by %.1f %.1f\n", typePath ? "type path" : "vector mask", id, sub,
                anchor, dx, dy);
   Commit();
   return true;
}

bool Scene2DNode::ApplyTextBox(int id, const double box[4], float* outW, float* outH)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text)
      return false;
   auto num = [](double v) { return std::isfinite(v) ? v : 0.0; };
   auto size = [&](double v) { return (float)std::clamp(num(v), 1.0, (double)std::numeric_limits<float>::max()); };
   const float w = size(box[2]), h = size(box[3]);
   l->ax = l->ay = 0.0f;
   l->x = (float)num(box[0]);
   l->y = (float)num(box[1]);
   l->boxW = w;
   if (outW != nullptr)
      *outW = w;
   if (outH != nullptr)
      *outH = h;
   std::fprintf(stderr, "PROBE apply box layer %d at %.1f %.1f size %g %g\n", id, l->x, l->y, w, h);
   Commit();
   return true;
}

bool Scene2DNode::ConvertTextKind(int id, bool paragraph)
{
   Layers();
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text || l->typeMode != 0 || (l->boxW > 0.0f) == paragraph)
      return false;
   float lo[2], hi[2];
   if (!Bounds(id, lo, hi))
      return false;
   int w = 0, h = 0;
   TextSize(*l, w, h);
   int hardened = 0;
   if (!paragraph && !l->vertical && !l->noBreak)
   {
      // Paragraph -> point keeps the lines: each soft wrap becomes a line break (PhotoCraft hardens them the same way).
      // A word starts a new line when the box text up to it is a line taller than the text before it.
      SyncTextRuns(*l);
      const float pitch = std::max(1.0f, l->fontSize * std::max(0.5f, l->leading));
      auto heightTo = [&](size_t n)
      {
         Layer t = *l;
         t.text = l->text.substr(0, n);
         size_t left = n;
         for (TextRun& r : t.runs)
         {
            r.len = std::min(r.len, left);
            left -= r.len;
         }
         t.runText = t.text;
         int tw = 0, th = 0;
         TextSize(t, tw, th);
         return th;
      };
      std::vector<size_t> breaks;
      int prev = -1;
      for (size_t i = 0; i <= l->text.size(); i++)
      {
         const bool end = i == l->text.size() || l->text[i] == ' ' || l->text[i] == '\n';
         if (!end || i == 0 || l->text[i - 1] == ' ' || l->text[i - 1] == '\n')
            continue;
         // [word start, i) is a word; its start is after the last space or line break.
         size_t ws = l->text.find_last_of(" \n", i - 1);
         ws = ws == std::string::npos ? 0 : ws + 1;
         const int hNow = heightTo(i);
         if (prev >= 0 && ws > 0 && l->text[ws - 1] == ' ' && (float)(hNow - prev) > pitch * 0.75f)
            breaks.push_back(ws);
         prev = hNow;
      }
      for (size_t k = breaks.size(); k-- > 0;)
         ReplaceRunText(*l, breaks[k], breaks[k], "\n");
      hardened = (int)breaks.size();
      TextSize(*l, w, h);
   }
   // A box a pixel wider than the lines (no new wraps); vertical type keeps its columns, the box its column height.
   l->boxW = paragraph ? (float)(l->vertical ? h : w) + 1.0f : 0.0f;
   if (paragraph && !l->vertical)
   {
      // The measured width is the ink's, a little narrower than the advances the lines wrap at: widen the box until
      // the text is no taller than it was (no new wrap): a pixel at a time first, so the box hugs the ink, then a
      // tenth of the size at a time.
      const float coarse = std::max(1.0f, l->fontSize * 0.1f);
      int bw = 0, bh = 0;
      for (int tries = 0; tries < 48 && TextSize(*l, bw, bh) && bh > h + 1; tries++)
         l->boxW += tries < 8 ? 1.0f : coarse;
   }
   float lo2[2], hi2[2];
   Bounds(id, lo2, hi2);
   // Keep the ink where it was: the side the alignment holds (left, centre or right) stays put.
   const float holdX = l->vertical ? lo[0] - lo2[0]
                       : l->align == 1 ? (lo[0] + hi[0] - lo2[0] - hi2[0]) * 0.5f
                       : l->align == 2 ? hi[0] - hi2[0]
                                       : lo[0] - lo2[0];
   l->x += holdX;
   l->y += lo[1] - lo2[1];
   std::fprintf(stderr, "PROBE convert layer %d to %s: box %.0f moved %.1f %.1f, %d soft wrap(s) hardened\n", id,
                paragraph ? "paragraph" : "point", l->boxW, holdX, lo[1] - lo2[1], hardened);
   mLastHardened = hardened;
   Commit();
   return true;
}

const char* Scene2DNode::NewTypeLayerName(bool mask, bool vertical)
{
   return mask ? "Type Mask" : vertical ? "New Vertical Type Layer" : "New text";
}
