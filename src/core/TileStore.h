#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Painted pixels, kept out of node params. A paint surface is cut into tiles;
// each tile is stored once, PNG-compressed, under a key made from its
// content ("t" + 16 hex digits; "-" is an empty tile). A layer's saved text
// holds only keys, so an undo snapshot stays small and restoring one restores
// the picture. Patch files carry the tiles they use as `blob` lines.
namespace TileStore
{
   constexpr int kTile = 256;

   // Stores straight RGBA8 (w x h, rows bottom-up as read back from GL) and
   // returns its key; "-" when every pixel is transparent.
   std::string Put(const uint8_t* rgba, int w, int h);
   // A tile read from a file, already PNG-encoded.
   void PutEncoded(const std::string& key, std::vector<uint8_t> png);
   // Decodes a stored tile; false when the key is unknown or "-".
   bool Get(const std::string& key, std::vector<uint8_t>& rgba, int& w, int& h);
   // The PNG bytes behind a key, empty when unknown.
   std::vector<uint8_t> Encoded(const std::string& key);
   bool Has(const std::string& key);

   bool IsKey(const char* s, size_t n);
   // Every tile key in `text` that the store holds, appended once each.
   void CollectKeys(const std::string& text, std::vector<std::string>& out);
}
