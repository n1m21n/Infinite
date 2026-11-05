#include "ChatFiles.h"

#include "audio/MediaExtensions.h"

#include <algorithm>

namespace chat
{
   namespace
   {
      std::string Ext(const std::string& path)
      {
         const size_t slash = path.find_last_of("/\\");
         const size_t dot = path.rfind('.');
         if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return "";
         std::string e = path.substr(dot + 1);
         std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
         return e;
      }

      bool In(const std::string& e, const std::vector<std::string>& list) { return std::find(list.begin(), list.end(), e) != list.end(); }

      // The canvas drop's lists (StageDropHandling.cpp), then the text a code node or the agent reads.
      const std::vector<std::string> kModel = { "obj", "ply", "stl", "usd", "usda", "usdc", "usdz", "abc", "gltf", "glb" };
      const std::vector<std::string> kText = { "txt", "md", "json", "csv", "glsl", "frag", "vert", "fs", "isf",
                                               "js", "py", "lua", "c", "cpp", "h", "hpp", "xml", "yaml", "yml", "toml" };
      const std::vector<std::string> kOther = { "mid", "midi", "svg", "inf", "infinite", "field", "infdev" };
   }

   std::string FileKind(const std::string& path)
   {
      const std::string e = Ext(path);
      if (e.empty())
         return "";
      if (In(e, MediaExtensions::Audio()))
         return "audio";
      if (In(e, kModel))
         return "model";
      if (In(e, MediaExtensions::Video()))
         return "video";
      if (In(e, MediaExtensions::Image()))
         return "image";
      if (e == "mid" || e == "midi")
         return "midi";
      if (e == "svg")
         return "vector";
      if (e == "inf" || e == "infinite")
         return "patch";
      if (e == "field" || e == "infdev")
         return "field";
      if (In(e, kText))
         return "text";
      return "";
   }

   std::string WithFiles(const std::string& prompt, const std::vector<std::string>& files)
   {
      if (files.empty())
         return prompt;
      std::string out = prompt + "\n\nAttached files (read them from these paths):";
      for (const std::string& f : files)
         out += "\n- " + f + " (" + FileKind(f) + ")";
      return out;
   }

   std::vector<std::string> FileDirs(const std::vector<std::string>& files)
   {
      std::vector<std::string> dirs;
      for (const std::string& f : files)
      {
         const size_t slash = f.find_last_of("/\\");
         const std::string d = slash == std::string::npos ? "." : f.substr(0, std::max<size_t>(1, slash));
         if (std::find(dirs.begin(), dirs.end(), d) == dirs.end())
            dirs.push_back(d);
      }
      return dirs;
   }

   std::vector<std::string> AttachExtensions()
   {
      std::vector<std::string> out;
      for (const std::vector<std::string>* list : { &MediaExtensions::Audio(), &MediaExtensions::Video(), &MediaExtensions::Image(), &kModel, &kOther, &kText })
         for (const std::string& e : *list)
            if (!In(e, out))
               out.push_back(e);
      return out;
   }
}
