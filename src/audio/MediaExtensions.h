#pragma once

#include <string>
#include <vector>

// The video/image extension lists, shared verbatim by the OS file-drop
// handler (main.cpp), the canvas drag-and-drop resolution logic (main.cpp),
// and the Media mode of SampleScanner - so all three ways a media file can
// enter the graph classify a given path identically. No dot prefix (".mov"),
// matching main.cpp's HasExtension convention.
//
// Video is checked before image wherever both lists are consulted together,
// since the image branch is typically the fallback/"everything else" case.
namespace MediaExtensions
{
   inline const std::vector<std::string>& Video()
   {
      static const std::vector<std::string> kExts = { "mov", "mp4", "m4v", "avi", "mkv",
                                                        "webm", "mpg", "mpeg", "wmv", "flv", "hevc" };
      return kExts;
   }

   // Everything Platform::LoadImageRGBA can decode (ImageIO/NSImage on
   // macOS, not stb_image) - see Platform.h.
   inline const std::vector<std::string>& Image()
   {
      static const std::vector<std::string> kExts = { "png", "jpg", "jpeg", "heic", "heif", "tiff",
                                                        "tif", "bmp", "gif", "psd", "webp", "exr", "hdr" };
      return kExts;
   }
}
