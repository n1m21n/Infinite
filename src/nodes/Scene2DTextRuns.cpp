// Styled runs inside a text layer, and named Character / Paragraph Styles.
// The model follows PhotoCraft's crates/doc/src/text_styles.rs and its type-style commands (Apache License 2.0,
// Copyright the PhotoCraft authors): a run's look resolves defaults <- Basic Paragraph <- paragraph style <-
// character style <- the run's own overrides, and restyling keeps the overrides.
// The layer's own font / size / colour / bold / italic / underline / tracking fields stay the first run's, so every
// older path that writes those fields (the Text card, modulation, presets) keeps working: SyncTextRuns folds a field
// change into every run, and a text change into the runs around it.
#include "Scene2DNode.h"

#include <cstdio>
#include <limits>
#include <sstream>

namespace
{
   using Node = Scene2DNode;

   bool IsCont(unsigned char c) { return (c & 0xC0) == 0x80; }
   size_t SnapBack(const std::string& s, size_t at)
   {
      at = std::min(at, s.size());
      while (at > 0 && at < s.size() && IsCont((unsigned char)s[at]))
         at--;
      return at;
   }

   std::string Num(float v)
   {
      char b[32];
      std::snprintf(b, sizeof b, "%.9g", (double)v);
      return b;
   }
   bool ReadFloat(const std::string& t, float& out)
   {
      std::istringstream s(t);
      float v = 0.0f;
      std::string rest;
      if (!(s >> v) || (s >> rest) || !std::isfinite(v))
         return false;
      out = v;
      return true;
   }
   bool ReadBool(const std::string& t, bool& out)
   {
      if (t == "1" || t == "true")
         out = true;
      else if (t == "0" || t == "false")
         out = false;
      else
         return false;
      return true;
   }
   bool Fail(std::string* err, const std::string& why)
   {
      if (err)
         *err = why;
      return false;
   }

   // Makes `at` a run boundary; the index of the run that starts there (runs.size() at the end).
   size_t Split(std::vector<Node::TextRun>& runs, size_t at)
   {
      size_t pos = 0;
      for (size_t i = 0; i < runs.size(); i++)
      {
         if (at == pos)
            return i;
         if (at < pos + runs[i].len)
         {
            Node::TextRun tail = runs[i];
            tail.len = pos + runs[i].len - at;
            runs[i].len = at - pos;
            runs.insert(runs.begin() + (long)i + 1, tail);
            return i + 1;
         }
         pos += runs[i].len;
      }
      return runs.size();
   }

   // Replaces bytes [a, b) of the runs with n bytes; the new bytes take the first replaced character's style, or at a
   // caret the one before it (the first run's at the start).
   void ReplaceRuns(std::vector<Node::TextRun>& runs, size_t a, size_t b, size_t n)
   {
      if (runs.empty())
         runs.push_back({});
      Node::RunStyle st = runs[Node::RunAt(runs, a < b ? a : (a > 0 ? a - 1 : 0))].st;
      if (a == 0 && a == b)
         st = runs[0].st;
      const size_t i = Split(runs, a), j = Split(runs, b);
      runs.erase(runs.begin() + (long)i, runs.begin() + (long)j);
      if (n > 0)
      {
         const size_t k = Split(runs, a);
         runs.insert(runs.begin() + (long)k, Node::TextRun{ n, st });
      }
      if (runs.empty())
         runs.push_back({ 0, st });
   }

   // Every text layer follows a style change from `old` to `nw`, keeping its overrides.
   void RestyleAll(std::vector<Node::Layer>& layers, const Node::TextStyles& old, const Node::TextStyles& nw)
   {
      for (Node::Layer& l : layers)
      {
         if (l.kind != Node::Kind::Text)
            continue;
         Node::SyncTextRuns(l);
         const uint32_t p = l.paraStyle;
         Node::SetPara(l, nw.RestylePara(old, Node::ParaOf(l), p, p));
         for (Node::TextRun& r : l.runs)
            r.st = nw.RestyleChar(old, r.st, p, p, r.st.charStyle);
         Node::NormalizeRuns(l);
      }
   }
}

// ---- styles -----------------------------------------------------------------

const Scene2DNode::CharStyleDef* Scene2DNode::TextStyles::Char(uint32_t id) const
{
   for (const CharStyleDef& d : character)
      if (d.id == id)
         return &d;
   return nullptr;
}
Scene2DNode::CharStyleDef* Scene2DNode::TextStyles::Char(uint32_t id)
{
   return const_cast<CharStyleDef*>(static_cast<const TextStyles*>(this)->Char(id));
}
const Scene2DNode::ParaStyleDef* Scene2DNode::TextStyles::Para(uint32_t id) const
{
   if (id == 0)
      return &basic;
   for (const ParaStyleDef& d : paragraph)
      if (d.id == id)
         return &d;
   return nullptr;
}
Scene2DNode::ParaStyleDef* Scene2DNode::TextStyles::Para(uint32_t id)
{
   return const_cast<ParaStyleDef*>(static_cast<const TextStyles*>(this)->Para(id));
}

bool Scene2DNode::TextStyles::NextCharId(uint32_t& out) const
{
   uint32_t hi = 0;
   for (const CharStyleDef& d : character)
      hi = std::max(hi, d.id);
   if (hi == std::numeric_limits<uint32_t>::max())
      return false;
   out = hi + 1;
   return true;
}
bool Scene2DNode::TextStyles::NextParaId(uint32_t& out) const
{
   uint32_t hi = 0;
   for (const ParaStyleDef& d : paragraph)
      hi = std::max(hi, d.id);
   if (hi == std::numeric_limits<uint32_t>::max())
      return false;
   out = hi + 1;
   return true;
}

std::string Scene2DNode::TextStyles::UniqueName(bool para, const std::string& base) const
{
   auto used = [&](const std::string& n)
   {
      if (para)
      {
         if (n == basic.name)
            return true;
         for (const ParaStyleDef& d : paragraph)
            if (d.name == n)
               return true;
         return false;
      }
      if (n == kNoCharStyle)
         return true;
      for (const CharStyleDef& d : character)
         if (d.name == n)
            return true;
      return false;
   };
   const bool generic = base == "Character Style" || base == "Paragraph Style";
   if (!generic && !base.empty() && !used(base))
      return base;
   for (int n = 1;; n++)
   {
      const std::string c = base + " " + std::to_string(n);
      if (!used(c))
         return c;
   }
}

Scene2DNode::ParaStyle Scene2DNode::TextStyles::ResolvePara(uint32_t para) const
{
   ParaStyle p;
   ApplyParaAttrs(p, basic.para);
   if (para != 0)
      if (const ParaStyleDef* d = Para(para))
         ApplyParaAttrs(p, d->para);
   return p;
}

Scene2DNode::RunStyle Scene2DNode::TextStyles::ResolveChar(uint32_t para, uint32_t chr) const
{
   RunStyle r;
   ApplyCharAttrs(r, basic.chr);
   if (para != 0)
      if (const ParaStyleDef* d = Para(para))
         ApplyCharAttrs(r, d->chr);
   if (const CharStyleDef* c = chr != 0 ? Char(chr) : nullptr)
   {
      ApplyCharAttrs(r, c->attrs);
      r.charStyle = chr;
   }
   return r;
}

Scene2DNode::StyleAttrs Scene2DNode::TextStyles::CharOverrides(const RunStyle& run, uint32_t para) const
{
   return DiffAttrs(CharAttrsOf(run), CharAttrsOf(ResolveChar(para, run.charStyle)));
}
Scene2DNode::StyleAttrs Scene2DNode::TextStyles::ParaOverrides(const ParaStyle& p, uint32_t para) const
{
   return DiffAttrs(ParaAttrsOf(p), ParaAttrsOf(ResolvePara(para)));
}

Scene2DNode::RunStyle Scene2DNode::TextStyles::RestyleChar(const TextStyles& old, const RunStyle& run, uint32_t oldPara,
                                                           uint32_t newPara, uint32_t newChr) const
{
   const StyleAttrs ov = old.CharOverrides(run, oldPara);
   RunStyle r = ResolveChar(newPara, newChr);
   ApplyCharAttrs(r, ov);
   return r;
}
Scene2DNode::ParaStyle Scene2DNode::TextStyles::RestylePara(const TextStyles& old, const ParaStyle& p, uint32_t oldPara,
                                                            uint32_t newPara) const
{
   const StyleAttrs ov = old.ParaOverrides(p, oldPara);
   ParaStyle r = ResolvePara(newPara);
   ApplyParaAttrs(r, ov);
   return r;
}

// ---- attribute sets -----------------------------------------------------------

bool Scene2DNode::ApplyCharAttrs(RunStyle& out, const StyleAttrs& a, std::string* err)
{
   RunStyle s = out;
   for (const auto& [k, v] : a)
   {
      if (k == "font")
         s.font = v;
      else if (k == "size")
      {
         if (!ReadFloat(v, s.size) || s.size < 1.0f || s.size > 2000.0f)
            return Fail(err, "size must be 1 to 2000");
      }
      else if (k == "tracking")
      {
         if (!ReadFloat(v, s.tracking) || std::fabs(s.tracking) > 1000.0f)
            return Fail(err, "tracking must be -1000 to 1000");
      }
      else if (k == "color")
      {
         std::istringstream in(v);
         float c[4];
         std::string rest;
         for (float& x : c)
            if (!(in >> x) || !std::isfinite(x) || x < 0.0f || x > 1.0f)
               return Fail(err, "color takes four numbers 0 to 1");
         if (in >> rest)
            return Fail(err, "color takes four numbers 0 to 1");
         std::copy(c, c + 4, s.c);
      }
      else if (k == "bold" || k == "italic" || k == "underline")
      {
         bool& f = k == "bold" ? s.bold : k == "italic" ? s.italic : s.underline;
         if (!ReadBool(v, f))
            return Fail(err, k + " must be 0 or 1");
      }
      else
         return Fail(err, "unknown character attribute " + k);
   }
   out = s;
   return true;
}

bool Scene2DNode::ApplyParaAttrs(ParaStyle& out, const StyleAttrs& a, std::string* err)
{
   ParaStyle s = out;
   for (const auto& [k, v] : a)
   {
      if (k == "align")
      {
         float f = 0.0f;
         if (!ReadFloat(v, f) || f != std::floor(f) || f < 0.0f || f > 3.0f)
            return Fail(err, "align must be 0 to 3");
         s.align = (int)f;
      }
      else if (k == "leading")
      {
         if (!ReadFloat(v, s.leading) || s.leading < 0.0f || s.leading > 100.0f)
            return Fail(err, "leading must be 0 to 100");
      }
      else if (k == "indentL" || k == "indentR" || k == "indentFirst" || k == "spaceBefore" || k == "spaceAfter")
      {
         float& f = k == "indentL" ? s.indentL : k == "indentR" ? s.indentR : k == "indentFirst" ? s.indentFirst
                    : k == "spaceBefore" ? s.spaceBefore : s.spaceAfter;
         if (!ReadFloat(v, f) || std::fabs(f) > 100000.0f)
            return Fail(err, k + " must be a number of pixels");
      }
      else
         return Fail(err, "unknown paragraph attribute " + k);
   }
   out = s;
   return true;
}

Scene2DNode::StyleAttrs Scene2DNode::CharAttrsOf(const RunStyle& s)
{
   StyleAttrs a;
   a["font"] = s.font;
   a["size"] = Num(s.size);
   a["tracking"] = Num(s.tracking);
   a["color"] = Num(s.c[0]) + " " + Num(s.c[1]) + " " + Num(s.c[2]) + " " + Num(s.c[3]);
   a["bold"] = s.bold ? "1" : "0";
   a["italic"] = s.italic ? "1" : "0";
   a["underline"] = s.underline ? "1" : "0";
   return a;
}
Scene2DNode::StyleAttrs Scene2DNode::ParaAttrsOf(const ParaStyle& s)
{
   StyleAttrs a;
   a["align"] = std::to_string(s.align);
   a["leading"] = Num(s.leading);
   a["indentL"] = Num(s.indentL);
   a["indentR"] = Num(s.indentR);
   a["indentFirst"] = Num(s.indentFirst);
   a["spaceBefore"] = Num(s.spaceBefore);
   a["spaceAfter"] = Num(s.spaceAfter);
   return a;
}
Scene2DNode::StyleAttrs Scene2DNode::DiffAttrs(const StyleAttrs& a, const StyleAttrs& base)
{
   StyleAttrs d;
   for (const auto& [k, v] : a)
   {
      auto it = base.find(k);
      if (it == base.end() || it->second != v)
         d[k] = v;
   }
   return d;
}

std::string Scene2DNode::AttrsText(const StyleAttrs& a)
{
   auto esc = [](const std::string& s)
   {
      std::string o;
      for (char c : s)
      {
         if (c == '\\' || c == ';' || c == '=')
            o += '\\';
         o += c;
      }
      return o;
   };
   std::string o;
   for (const auto& [k, v] : a)
   {
      if (!o.empty())
         o += ';';
      o += esc(k) + "=" + esc(v);
   }
   return o;
}
bool Scene2DNode::ParseAttrsText(const std::string& t, StyleAttrs& out)
{
   out.clear();
   std::string k, v, *cur = &k;
   bool haveEq = false;
   auto flush = [&]()
   {
      if (!haveEq || k.empty())
         return false;
      out[k] = v;
      k.clear();
      v.clear();
      cur = &k;
      haveEq = false;
      return true;
   };
   for (size_t i = 0; i < t.size(); i++)
   {
      const char c = t[i];
      if (c == '\\' && i + 1 < t.size())
         *cur += t[++i];
      else if (c == '=' && !haveEq)
      {
         haveEq = true;
         cur = &v;
      }
      else if (c == ';')
      {
         if (!flush())
            return false;
      }
      else
         *cur += c;
   }
   if (!k.empty() || haveEq)
      return flush();
   return true;
}

// ---- runs -----------------------------------------------------------------------

Scene2DNode::RunStyle Scene2DNode::SummaryOf(const Layer& l)
{
   RunStyle s;
   s.font = l.font;
   s.size = l.fontSize;
   s.tracking = l.tracking;
   std::copy(l.textC, l.textC + 4, s.c);
   s.bold = l.bold;
   s.italic = l.italic;
   s.underline = l.underline;
   s.charStyle = l.runs.empty() ? 0 : l.runs[0].st.charStyle;
   return s;
}
void Scene2DNode::SetSummary(Layer& l, const RunStyle& s)
{
   l.font = s.font;
   l.fontSize = s.size;
   l.tracking = s.tracking;
   std::copy(s.c, s.c + 4, l.textC);
   l.bold = s.bold;
   l.italic = s.italic;
   l.underline = s.underline;
}
Scene2DNode::ParaStyle Scene2DNode::ParaOf(const Layer& l)
{
   ParaStyle p;
   p.align = l.align;
   p.leading = l.leading;
   p.indentL = l.indentL;
   p.indentR = l.indentR;
   p.indentFirst = l.indentFirst;
   p.spaceBefore = l.spaceBefore;
   p.spaceAfter = l.spaceAfter;
   return p;
}
void Scene2DNode::SetPara(Layer& l, const ParaStyle& p)
{
   l.align = p.align;
   l.leading = p.leading;
   l.indentL = p.indentL;
   l.indentR = p.indentR;
   l.indentFirst = p.indentFirst;
   l.spaceBefore = p.spaceBefore;
   l.spaceAfter = p.spaceAfter;
}

size_t Scene2DNode::RunAt(const std::vector<TextRun>& runs, size_t at)
{
   size_t pos = 0;
   for (size_t i = 0; i < runs.size(); i++)
   {
      if (at < pos + runs[i].len)
         return i;
      pos += runs[i].len;
   }
   return runs.empty() ? 0 : runs.size() - 1;
}

void Scene2DNode::NormalizeRuns(Layer& l)
{
   if (l.runs.empty())
      l.runs.push_back({ 0, SummaryOf(l) });
   const RunStyle first = l.runs[0].st;
   // The runs cover exactly the text: a short list stretches its last run, a long one is cut from the end.
   size_t sum = 0;
   for (TextRun& r : l.runs)
   {
      r.len = std::min(r.len, l.text.size() - std::min(sum, l.text.size()));
      sum += r.len;
   }
   if (sum < l.text.size())
      l.runs.back().len += l.text.size() - sum;
   // Run edges sit on characters.
   size_t pos = 0;
   for (size_t i = 0; i + 1 < l.runs.size(); i++)
   {
      const size_t end = pos + l.runs[i].len, snapped = SnapBack(l.text, end);
      if (snapped != end)
      {
         l.runs[i + 1].len += end - snapped;
         l.runs[i].len -= end - snapped;
      }
      pos += l.runs[i].len;
   }
   std::vector<TextRun> out;
   for (const TextRun& r : l.runs)
   {
      if (r.len == 0)
         continue;
      if (!out.empty() && out.back().st == r.st)
         out.back().len += r.len;
      else
         out.push_back(r);
   }
   if (out.empty())
      out.push_back({ 0, first });
   l.runs = std::move(out);
   l.runSummary = l.runs[0].st;
   SetSummary(l, l.runSummary);
   l.runText = l.text;
}

void Scene2DNode::SyncTextRuns(Layer& l)
{
   if (l.runs.empty())
   {
      l.runs.push_back({ l.text.size(), SummaryOf(l) });
      l.runText = l.text;
      NormalizeRuns(l);
      return;
   }
   // Summary-field edits since the last sync go to every run (size as a ratio, so mixed sizes scale together).
   const RunStyle cur = SummaryOf(l), was = l.runSummary;
   if (!(cur == was))
      for (TextRun& r : l.runs)
      {
         RunStyle& s = r.st;
         if (cur.font != was.font)
            s.font = cur.font;
         if (cur.size != was.size)
            s.size = l.runs.size() == 1 || !(was.size > 0.0f) ? cur.size : std::clamp(s.size * (cur.size / was.size), 1.0f, 2000.0f);
         if (cur.tracking != was.tracking)
            s.tracking = cur.tracking;
         if (!std::equal(cur.c, cur.c + 4, was.c))
            std::copy(cur.c, cur.c + 4, s.c);
         if (cur.bold != was.bold)
            s.bold = cur.bold;
         if (cur.italic != was.italic)
            s.italic = cur.italic;
         if (cur.underline != was.underline)
            s.underline = cur.underline;
      }
   // A text edit made straight on the field: the changed middle is replaced, the prefix and suffix keep their runs.
   if (l.runText != l.text)
   {
      const std::string& o = l.runText;
      const std::string& n = l.text;
      size_t p = 0;
      while (p < o.size() && p < n.size() && o[p] == n[p])
         p++;
      p = SnapBack(o, p);
      size_t s = 0;
      while (s < o.size() - p && s < n.size() - p && o[o.size() - 1 - s] == n[n.size() - 1 - s])
         s++;
      while (s > 0 && o.size() - s < o.size() && IsCont((unsigned char)o[o.size() - s]))
         s--;
      // The runs were cut for runText; keep them covering it before the replace.
      size_t sum = 0;
      for (const TextRun& r : l.runs)
         sum += r.len;
      if (sum != o.size())
      {
         std::string keep = l.text;
         l.text = o;
         NormalizeRuns(l);
         l.text = keep;
      }
      ReplaceRuns(l.runs, p, o.size() - s, n.size() - s - p);
   }
   NormalizeRuns(l);
}

void Scene2DNode::ReplaceRunText(Layer& l, size_t a, size_t b, const std::string& ins)
{
   SyncTextRuns(l);
   if (a > b)
      std::swap(a, b);
   a = SnapBack(l.text, a);
   b = SnapBack(l.text, b);
   ReplaceRuns(l.runs, a, b, ins.size());
   l.text.replace(a, b - a, ins);
   NormalizeRuns(l);
}

void Scene2DNode::EditRunText(Layer& l, const std::string& now, size_t hintLo)
{
   SyncTextRuns(l);
   const std::string& o = l.text;
   if (o == now)
      return;
   size_t p = 0;
   while (p < o.size() && p < now.size() && o[p] == now[p])
      p++;
   p = SnapBack(o, std::min(p, hintLo));
   size_t s = 0;
   while (s < o.size() - p && s < now.size() - p && o[o.size() - 1 - s] == now[now.size() - 1 - s])
      s++;
   while (s > 0 && IsCont((unsigned char)o[o.size() - s]))
      s--;
   ReplaceRunText(l, p, o.size() - s, now.substr(p, now.size() - s - p));
}

void Scene2DNode::StyleRange(Layer& l, size_t a, size_t b, const std::function<void(RunStyle&)>& f)
{
   SyncTextRuns(l);
   if (a > b)
      return;
   if (l.text.empty())
   {
      f(l.runs[0].st);
      NormalizeRuns(l);
      return;
   }
   a = SnapBack(l.text, a);
   b = SnapBack(l.text, b);
   if (a == b)
      return;
   const size_t i = Split(l.runs, a), j = Split(l.runs, b);
   for (size_t k = i; k < j; k++)
      f(l.runs[k].st);
   NormalizeRuns(l);
}

std::vector<Scene2DNode::TextRun> Scene2DNode::CharRuns(const Layer& l)
{
   Layer c;
   c.kind = Kind::Text;
   c.text = l.text;
   c.runText = l.runText;
   c.runs = l.runs;
   c.runSummary = l.runSummary;
   SetSummary(c, SummaryOf(l));
   SyncTextRuns(c);
   return c.runs;
}

// ---- through the node -------------------------------------------------------------

bool Scene2DNode::ReplaceText(int id, size_t a, size_t b, const std::string& ins)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text)
      return false;
   ReplaceRunText(*l, a, b, ins);
   Commit();
   return true;
}

bool Scene2DNode::StyleTextRange(int id, size_t a, size_t b, const StyleAttrs& set, std::string* err)
{
   Layer* l = FindLayer(id);
   if (l == nullptr || l->kind != Kind::Text)
      return Fail(err, "not a text layer");
   RunStyle probe;
   if (!ApplyCharAttrs(probe, set, err))
      return false;
   StyleRange(*l, a, b, [&](RunStyle& s) { ApplyCharAttrs(s, set); });
   Commit();
   return true;
}

bool Scene2DNode::NewCharStyle(int fromLayer, size_t at, uint32_t& outId, std::string* err, const StyleAttrs* attrs)
{
   Layers();
   TextStyles& ts = mDoc.textStyles;
   CharStyleDef d;
   if (!ts.NextCharId(d.id))
      return Fail(err, "character style id space exhausted");
   if (attrs)
   {
      RunStyle probe;
      if (!ApplyCharAttrs(probe, *attrs, err))
         return false;
      d.attrs = *attrs;
   }
   else if (const Layer* l = fromLayer != 0 ? FindLayer(fromLayer) : nullptr)
   {
      if (l->kind != Kind::Text)
         return Fail(err, "not a text layer");
      const std::vector<TextRun> runs = CharRuns(*l);
      d.attrs = DiffAttrs(CharAttrsOf(runs[RunAt(runs, at)].st), CharAttrsOf(ts.ResolveChar(l->paraStyle, 0)));
   }
   d.name = ts.UniqueName(false, "Character Style");
   ts.character.push_back(d);
   outId = d.id;
   Commit();
   return true;
}

bool Scene2DNode::NewParaStyle(int fromLayer, size_t at, uint32_t& outId, std::string* err, const StyleAttrs* paraAttrs,
                               const StyleAttrs* charAttrs)
{
   Layers();
   TextStyles& ts = mDoc.textStyles;
   ParaStyleDef d;
   if (!ts.NextParaId(d.id))
      return Fail(err, "paragraph style id space exhausted");
   if (paraAttrs || charAttrs)
   {
      ParaStyle pp;
      RunStyle cp;
      if ((paraAttrs && !ApplyParaAttrs(pp, *paraAttrs, err)) || (charAttrs && !ApplyCharAttrs(cp, *charAttrs, err)))
         return false;
      if (paraAttrs)
         d.para = *paraAttrs;
      if (charAttrs)
         d.chr = *charAttrs;
   }
   else if (const Layer* l = fromLayer != 0 ? FindLayer(fromLayer) : nullptr)
   {
      if (l->kind != Kind::Text)
         return Fail(err, "not a text layer");
      const std::vector<TextRun> runs = CharRuns(*l);
      d.para = DiffAttrs(ParaAttrsOf(ParaOf(*l)), ParaAttrsOf(ts.ResolvePara(0)));
      RunStyle base = ts.ResolveChar(0, 0);
      RunStyle st = runs[RunAt(runs, at)].st;
      st.charStyle = 0;
      d.chr = DiffAttrs(CharAttrsOf(st), CharAttrsOf(base));
   }
   d.name = ts.UniqueName(true, "Paragraph Style");
   ts.paragraph.push_back(d);
   outId = d.id;
   Commit();
   return true;
}

bool Scene2DNode::DuplicateStyle(bool para, uint32_t id, uint32_t& outId, std::string* err)
{
   Layers();
   TextStyles& ts = mDoc.textStyles;
   if (para)
   {
      const ParaStyleDef* src = ts.Para(id);
      if (src == nullptr)
         return Fail(err, "no such paragraph style");
      ParaStyleDef d = *src;
      if (!ts.NextParaId(d.id))
         return Fail(err, "paragraph style id space exhausted");
      d.name = ts.UniqueName(true, src->name + " copy");
      auto it = std::find_if(ts.paragraph.begin(), ts.paragraph.end(), [&](const ParaStyleDef& p) { return p.id == id; });
      ts.paragraph.insert(it == ts.paragraph.end() ? ts.paragraph.begin() : it + 1, d);
      outId = d.id;
   }
   else
   {
      const CharStyleDef* src = ts.Char(id);
      if (src == nullptr)
         return Fail(err, "no such character style");
      CharStyleDef d = *src;
      if (!ts.NextCharId(d.id))
         return Fail(err, "character style id space exhausted");
      d.name = ts.UniqueName(false, src->name + " copy");
      auto it = std::find_if(ts.character.begin(), ts.character.end(), [&](const CharStyleDef& c) { return c.id == id; });
      ts.character.insert(it + 1, d);
      outId = d.id;
   }
   Commit();
   return true;
}

bool Scene2DNode::DeleteStyle(bool para, uint32_t id)
{
   std::vector<Layer>& layers = Layers();
   TextStyles& ts = mDoc.textStyles;
   if (para ? (id == 0 || ts.Para(id) == nullptr) : ts.Char(id) == nullptr)
      return false;
   // The text keeps its look: what the style gave it becomes its own overrides.
   for (Layer& l : layers)
   {
      if (l.kind != Kind::Text)
         continue;
      SyncTextRuns(l);
      if (para && l.paraStyle == id)
         l.paraStyle = 0;
      if (!para)
         for (TextRun& r : l.runs)
            if (r.st.charStyle == id)
               r.st.charStyle = 0;
      NormalizeRuns(l);
   }
   if (para)
      ts.paragraph.erase(std::remove_if(ts.paragraph.begin(), ts.paragraph.end(), [&](const ParaStyleDef& p) { return p.id == id; }),
                         ts.paragraph.end());
   else
      ts.character.erase(std::remove_if(ts.character.begin(), ts.character.end(), [&](const CharStyleDef& c) { return c.id == id; }),
                         ts.character.end());
   Commit();
   return true;
}

bool Scene2DNode::RenameStyle(bool para, uint32_t id, const std::string& name)
{
   Layers();
   TextStyles& ts = mDoc.textStyles;
   if (name.empty() || (para && id == 0))
      return false;
   std::string* cur = nullptr;
   if (para)
   {
      if (ParaStyleDef* d = ts.Para(id))
         cur = &d->name;
   }
   else if (CharStyleDef* d = ts.Char(id))
      cur = &d->name;
   if (cur == nullptr)
      return false;
   if (*cur == name)
      return true;
   *cur = ts.UniqueName(para, name);
   Commit();
   return true;
}

bool Scene2DNode::ApplyCharStyle(int layer, size_t a, size_t b, uint32_t id, bool clearOverrides)
{
   Layer* l = FindLayer(layer);
   const TextStyles& ts = Doc().textStyles;
   if (l == nullptr || l->kind != Kind::Text || (id != 0 && ts.Char(id) == nullptr))
      return false;
   if (a == b)
   {
      a = 0;
      b = l->text.size();
   }
   const uint32_t p = l->paraStyle;
   StyleRange(*l, a, b, [&](RunStyle& s) { s = clearOverrides ? ts.ResolveChar(p, id) : ts.RestyleChar(ts, s, p, p, id); });
   Commit();
   return true;
}

bool Scene2DNode::ApplyParaStyle(int layer, uint32_t id, bool clearOverrides)
{
   Layer* l = FindLayer(layer);
   const TextStyles& ts = Doc().textStyles;
   if (l == nullptr || l->kind != Kind::Text || ts.Para(id) == nullptr)
      return false;
   SyncTextRuns(*l);
   const uint32_t old = l->paraStyle;
   SetPara(*l, clearOverrides ? ts.ResolvePara(id) : ts.RestylePara(ts, ParaOf(*l), old, id));
   for (TextRun& r : l->runs)
      r.st = clearOverrides ? ts.ResolveChar(id, r.st.charStyle) : ts.RestyleChar(ts, r.st, old, id, r.st.charStyle);
   l->paraStyle = id;
   NormalizeRuns(*l);
   Commit();
   return true;
}

bool Scene2DNode::RedefineStyle(bool para, uint32_t id, int fromLayer, size_t at)
{
   const Layer* l = FindLayer(fromLayer);
   if (l == nullptr || l->kind != Kind::Text)
      return false;
   const TextStyles& ts = mDoc.textStyles;
   const std::vector<TextRun> runs = CharRuns(*l);
   const RunStyle st = runs[RunAt(runs, at)].st;
   if (para)
   {
      if (ts.Para(id) == nullptr)
         return false;
      // Basic Paragraph holds all the text's look past the defaults; another style what it adds to Basic Paragraph.
      const StyleAttrs pa = DiffAttrs(ParaAttrsOf(ParaOf(*l)), ParaAttrsOf(id == 0 ? ParaStyle() : ts.ResolvePara(0)));
      RunStyle s = st;
      s.charStyle = 0;
      // The paragraph style takes the run's look under its character style, if it has one.
      StyleAttrs ca = DiffAttrs(CharAttrsOf(s), CharAttrsOf(id == 0 ? RunStyle() : ts.ResolveChar(0, 0)));
      if (const CharStyleDef* c = st.charStyle != 0 ? ts.Char(st.charStyle) : nullptr)
         for (const auto& kv : c->attrs)
            ca.erase(kv.first);
      return SetStyleAttrs(true, id, &pa, &ca);
   }
   if (ts.Char(id) == nullptr)
      return false;
   const StyleAttrs ca = DiffAttrs(CharAttrsOf(st), CharAttrsOf(ts.ResolveChar(l->paraStyle, 0)));
   return SetStyleAttrs(false, id, nullptr, &ca);
}

bool Scene2DNode::SetStyleAttrs(bool para, uint32_t id, const StyleAttrs* paraAttrs, const StyleAttrs* charAttrs, std::string* err)
{
   std::vector<Layer>& layers = Layers();
   TextStyles nw = mDoc.textStyles;
   if (para)
   {
      ParaStyleDef* d = nw.Para(id);
      if (d == nullptr)
         return Fail(err, "no such paragraph style");
      ParaStyle pp;
      RunStyle cp;
      if ((paraAttrs && !ApplyParaAttrs(pp, *paraAttrs, err)) || (charAttrs && !ApplyCharAttrs(cp, *charAttrs, err)))
         return false;
      if (paraAttrs)
         d->para = *paraAttrs;
      if (charAttrs)
         d->chr = *charAttrs;
   }
   else
   {
      CharStyleDef* d = nw.Char(id);
      if (d == nullptr)
         return Fail(err, "no such character style");
      if (paraAttrs && !paraAttrs->empty())
         return Fail(err, "a character style has no paragraph attributes");
      RunStyle cp;
      if (charAttrs && !ApplyCharAttrs(cp, *charAttrs, err))
         return false;
      if (charAttrs)
         d->attrs = *charAttrs;
   }
   if (nw == mDoc.textStyles)
      return true;
   RestyleAll(layers, mDoc.textStyles, nw);
   mDoc.textStyles = nw;
   Commit();
   return true;
}

bool Scene2DNode::PreviewStyleAttrs(bool para, uint32_t id, const StyleAttrs* paraAttrs, const StyleAttrs* charAttrs,
                                    std::string& outText, std::string* err)
{
   Layers();
   const std::vector<Layer> layersBak = mLayers;
   const Document docBak = mDoc;
   const std::string textBak = mText, parsedBak = mParsedText;
   const bool ok = SetStyleAttrs(para, id, paraAttrs, charAttrs, err);
   outText = ok ? mText : textBak;
   mLayers = layersBak;
   mDoc = docBak;
   mText = textBak;
   mParsedText = parsedBak;
   return ok;
}

std::string Scene2DNode::StyleLabel(int layer, size_t at, bool para)
{
   const Layer* l = FindLayer(layer);
   const TextStyles& ts = mDoc.textStyles;
   if (l == nullptr || l->kind != Kind::Text)
      return "";
   if (para)
   {
      const ParaStyleDef* d = ts.Para(l->paraStyle);
      const std::string n = d ? d->name : std::string(kBasicParagraph);
      return ts.ParaOverrides(ParaOf(*l), l->paraStyle).empty() ? n : n + "+";
   }
   const std::vector<TextRun> runs = CharRuns(*l);
   const RunStyle& st = runs[RunAt(runs, at)].st;
   const CharStyleDef* d = st.charStyle != 0 ? ts.Char(st.charStyle) : nullptr;
   const std::string n = d ? d->name : std::string(kNoCharStyle);
   return ts.CharOverrides(st, l->paraStyle).empty() ? n : n + "+";
}
