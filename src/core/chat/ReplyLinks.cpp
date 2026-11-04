#include "ReplyLinks.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

namespace chat
{
   namespace
   {
      bool WordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

      std::string Trim(const std::string& s)
      {
         size_t b = 0, e = s.size();
         while (b < e && std::isspace((unsigned char)s[b]))
            b++;
         while (e > b && std::isspace((unsigned char)s[e - 1]))
            e--;
         return s.substr(b, e - b);
      }

      std::string Shown(const CatalogNode& n) { return n.title.empty() ? n.type : n.title; }

      // The unique node whose title (or, failing that, type) is exactly `name`; -1 if none or several.
      int ByName(const Catalog& catalog, const std::string& name)
      {
         const std::string want = Lower(name);
         int hit = -1, count = 0;
         for (const CatalogNode& n : catalog.nodes)
            if (Lower(n.title) == want)
               hit = n.index, count++;
         if (count == 0)
            for (const CatalogNode& n : catalog.nodes)
               if (Lower(n.type) == want)
                  hit = n.index, count++;
         return count == 1 ? hit : -1;
      }

      void PushText(std::vector<Seg>& segs, const std::string& text, Seg::Kind kind = Seg::Kind::Text)
      {
         if (text.empty())
            return;
         if (!segs.empty() && segs.back().kind == kind && kind == Seg::Kind::Text)
         {
            segs.back().text += text;
            return;
         }
         Seg s;
         s.kind = kind;
         s.text = text;
         segs.push_back(s);
      }

      // `.key` right after a node link: a Param when the node has that key or label.
      bool TryKey(const std::string& line, size_t& i, const CatalogNode& n, std::vector<Seg>& segs)
      {
         if (i >= line.size() || line[i] != '.' || i + 1 >= line.size() || !WordChar(line[i + 1]))
            return false;
         size_t end = i + 1;
         while (end < line.size() && WordChar(line[end]))
            end++;
         const CatalogParam* p = n.Find(line.substr(i + 1, end - i - 1));
         if (p == nullptr)
            return false;
         segs.pop_back();  // the node chip becomes the param chip
         Seg s;
         s.kind = Seg::Kind::Param;
         s.node = n.index;
         s.key = p->key;
         s.text = Shown(n) + "." + (p->label.empty() ? p->key : p->label);
         segs.push_back(s);
         i = end;
         return true;
      }

      // One `@` reference at line[i] (the '@'). Returns false when it is not one, leaving i alone.
      bool TryRef(const std::string& line, size_t& i, const Catalog& catalog, std::vector<Seg>& segs, std::vector<int>& named)
      {
         const size_t from = i + 1;
         // `@title#index`: the index ends the title, so titles may hold spaces.
         const size_t hash = line.find('#', from);
         const size_t limit = std::min(line.size(), from + 64);
         if (hash != std::string::npos && hash < limit && hash > from && hash + 1 < line.size() && std::isdigit((unsigned char)line[hash + 1]))
         {
            size_t end = hash + 1;
            while (end < line.size() && std::isdigit((unsigned char)line[end]))
               end++;
            const std::string title = line.substr(from, hash - from);
            if (title.find('@') == std::string::npos && title.find('`') == std::string::npos)
            {
               const int index = std::atoi(line.substr(hash + 1, end - hash - 1).c_str());
               Seg s;
               const CatalogNode* n = catalog.Node(index);
               s.kind = n != nullptr ? Seg::Kind::Node : Seg::Kind::Missing;
               s.node = n != nullptr ? index : -1;
               s.text = n != nullptr ? Shown(*n) : title;
               // a pin (`:1`) stays part of the chip's text
               if (end + 1 < line.size() && line[end] == ':' && std::isdigit((unsigned char)line[end + 1]))
               {
                  size_t pinEnd = end + 1;
                  while (pinEnd < line.size() && std::isdigit((unsigned char)line[pinEnd]))
                     pinEnd++;
                  s.text += line.substr(end, pinEnd - end);
                  end = pinEnd;
               }
               segs.push_back(s);
               i = end;
               if (n != nullptr)
               {
                  named.push_back(index);
                  TryKey(line, i, *n, segs);
               }
               return true;
            }
         }
         // `@title` without an index: the longest unique title written there.
         const std::string lower = Lower(line);
         size_t best = 0;
         int hit = -1;
         for (const CatalogNode& n : catalog.nodes)
         {
            const std::string t = Lower(n.title);
            if (t.empty() || t.size() <= best || lower.compare(from, t.size(), t) != 0)
               continue;
            if (from + t.size() < line.size() && WordChar(line[from + t.size()]))
               continue;
            best = t.size();
            hit = ByName(catalog, n.title);
         }
         if (hit < 0)
            return false;
         Seg s;
         s.kind = Seg::Kind::Node;
         s.node = hit;
         s.text = Shown(*catalog.Node(hit));
         segs.push_back(s);
         i = from + best;
         named.push_back(hit);
         TryKey(line, i, *catalog.Node(hit), segs);
         return true;
      }

      std::vector<Seg> Inline(const std::string& line, const Catalog& catalog, std::vector<int>& named, std::vector<std::string>& images)
      {
         std::vector<Seg> segs;
         size_t i = 0;
         std::string text;
         auto flush = [&]() { PushText(segs, text); text.clear(); };
         while (i < line.size())
         {
            const char c = line[i];
            if (c == '@' && (i == 0 || !WordChar(line[i - 1])))
            {
               flush();
               if (TryRef(line, i, catalog, segs, named))
                  continue;
               text += c;
               i++;
               continue;
            }
            if (c == '`')
            {
               const size_t close = line.find('`', i + 1);
               if (close != std::string::npos)
               {
                  flush();
                  const std::string inner = line.substr(i + 1, close - i - 1);
                  size_t at = 0;
                  std::vector<Seg> linked;
                  if (!inner.empty() && inner[0] == '@' && TryRef(inner, at, catalog, linked, named) && at == inner.size())
                     segs.insert(segs.end(), linked.begin(), linked.end());
                  else if (const int node = ByName(catalog, inner); node >= 0)
                  {
                     Seg s;
                     s.kind = Seg::Kind::Node;
                     s.node = node;
                     s.text = Shown(*catalog.Node(node));
                     segs.push_back(s);
                     named.push_back(node);
                  }
                  else
                  {
                     PushText(segs, inner, Seg::Kind::Code);
                     if (IsImagePath(inner))
                        images.push_back(inner);
                  }
                  i = close + 1;
                  continue;
               }
            }
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '*')
            {
               const size_t close = line.find("**", i + 2);
               if (close != std::string::npos)
               {
                  flush();
                  PushText(segs, line.substr(i + 2, close - i - 2), Seg::Kind::Bold);
                  i = close + 2;
                  continue;
               }
            }
            text += c;
            i++;
         }
         flush();
         return segs;
      }

      // Bare keys of nodes named in this paragraph become param links: "raised @cube#1's detail".
      void LinkBareKeys(std::vector<Seg>& segs, const Catalog& catalog, const std::vector<int>& named)
      {
         if (named.empty())
            return;
         std::vector<Seg> out;
         for (const Seg& s : segs)
         {
            if (s.kind != Seg::Kind::Text)
            {
               out.push_back(s);
               continue;
            }
            size_t i = 0, last = 0;
            while (i < s.text.size())
            {
               if (!WordChar(s.text[i]) || (i > 0 && WordChar(s.text[i - 1])))
               {
                  i++;
                  continue;
               }
               size_t end = i;
               while (end < s.text.size() && WordChar(s.text[end]))
                  end++;
               const std::string word = s.text.substr(i, end - i);
               const CatalogParam* hit = nullptr;
               int node = -1, count = 0;
               if (word.size() >= 3)
                  for (int index : std::set<int>(named.begin(), named.end()))
                     for (const CatalogParam& p : catalog.Node(index)->params)
                        if (p.key == word)
                           hit = &p, node = index, count++;
               if (count == 1)
               {
                  PushText(out, s.text.substr(last, i - last));
                  Seg p;
                  p.kind = Seg::Kind::Param;
                  p.node = node;
                  p.key = hit->key;
                  p.text = hit->label.empty() ? hit->key : hit->label;
                  out.push_back(p);
                  last = end;
               }
               i = end;
            }
            PushText(out, s.text.substr(last));
         }
         segs = out;
      }
   }

   bool IsImagePath(const std::string& path)
   {
      const std::string l = Lower(Trim(path));
      for (const char* ext : { ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp" })
      {
         const std::string e = ext;
         if (l.size() > e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0)
            return l.find(' ') == std::string::npos || l[0] == '/' || l[0] == '~';
      }
      return false;
   }

   std::vector<Block> ParseReply(const std::string& text, const Catalog& catalog)
   {
      std::vector<Block> blocks;
      size_t start = 0;
      bool inCode = false;
      Block code;
      code.kind = Block::Kind::Code;
      std::vector<int> named;
      size_t paraStart = 0;  // first block of the current paragraph, for bare-key links
      while (start <= text.size())
      {
         size_t end = text.find('\n', start);
         if (end == std::string::npos)
            end = text.size();
         std::string line = text.substr(start, end - start);
         if (!line.empty() && line.back() == '\r')
            line.pop_back();
         start = end + 1;
         const std::string trimmed = Trim(line);
         if (trimmed.rfind("```", 0) == 0)
         {
            if (inCode)
            {
               if (!code.text.empty() && code.text.back() == '\n')
                  code.text.pop_back();
               blocks.push_back(code);
               code.text.clear();
            }
            inCode = !inCode;
            continue;
         }
         if (inCode)
         {
            code.text += line + "\n";
            continue;
         }
         if (trimmed.empty())
         {
            named.clear();
            paraStart = blocks.size();
            continue;
         }
         std::string bare = trimmed;
         if (bare.size() > 2 && bare.front() == '`' && bare.back() == '`')
            bare = bare.substr(1, bare.size() - 2);
         if (IsImagePath(bare))
         {
            Block b;
            b.kind = Block::Kind::Image;
            b.text = bare;
            blocks.push_back(b);
            continue;
         }
         Block b;
         std::string body = trimmed;
         if (body.rfind("- ", 0) == 0 || body.rfind("* ", 0) == 0)
         {
            b.kind = Block::Kind::Bullet;
            body = body.substr(2);
         }
         else if (body[0] == '#')
         {
            b.kind = Block::Kind::Heading;
            body = Trim(body.substr(body.find_first_not_of('#')));
         }
         std::vector<std::string> images;
         b.segs = Inline(body, catalog, named, images);
         // A bullet is its own sentence: its links do not reach the next bullet.
         if (b.kind == Block::Kind::Bullet)
         {
            LinkBareKeys(b.segs, catalog, named);
            named.clear();
         }
         blocks.push_back(b);
         for (const std::string& path : images)
         {
            Block img;
            img.kind = Block::Kind::Image;
            img.text = path;
            blocks.push_back(img);
         }
         if (b.kind != Block::Kind::Bullet)
            for (size_t k = paraStart; k < blocks.size(); k++)
               if (blocks[k].kind == Block::Kind::Para || blocks[k].kind == Block::Kind::Heading)
                  LinkBareKeys(blocks[k].segs, catalog, named);
      }
      if (inCode && !code.text.empty())
         blocks.push_back(code);
      return blocks;
   }
}
