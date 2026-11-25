#pragma once

// AI selection models from the "select" extension pack (internal/plans/2d-view/ai-tools.md, Block 1).
// SAM 2.1 tiny: an image encoder (about 1.3 s at 1024 x 1024 on an M2 CPU) whose result is cached per
// picture, and a prompt decoder (about 30 ms) run once per click or box. ONNX Runtime and the models come
// from the pack, never the installer; when the pack is absent every caller falls back to its classic tool.
//
// Threading: the encoder runs on one worker thread with a latest-only slot (a newer request replaces a
// queued one), never on the UI, GL or audio threads. Decode is cheap and runs on the caller's thread.
// The models unload after 60 s without use.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace AiSelect
{
   // The pack is installed and enabled and holds the runtime and the SAM files.
   bool Available();
   // Why it is not available, for the tool's hint ("" when it is).
   std::string Missing();

   // A picture as the encoder takes it: top-down RGBA, any size.
   struct Picture
   {
      int w = 0, h = 0;
      std::vector<uint8_t> rgba;
   };
   // A cheap content key: equal pictures give equal keys.
   uint64_t KeyOf(const Picture& p);

   // Asks for the picture's embedding. Returns at once; Ready(key) turns true when it is done.
   // Several requests while one runs keep only the newest.
   void Request(std::shared_ptr<const Picture> pic, uint64_t key);
   bool Ready(uint64_t key);
   bool Busy();               // a request is queued or running
   std::string LastError();   // the worker's last failure ("" after a success)

   // Blocking encode on the caller's thread (tests and the eval hook only).
   bool EncodeNow(const Picture& pic, uint64_t key, std::string& err);

   struct Prompt
   {
      std::vector<float> xy;      // points in picture pixels, x0 y0 x1 y1 ...
      std::vector<int> labels;    // 1 = keep, 0 = leave out, one per point
      bool hasBox = false;
      float box[4] = { 0, 0, 0, 0 };   // x0 y0 x1 y1 in picture pixels
   };
   // Decodes against the cached embedding of `key` (Ready(key) must be true). out: w x h coverage,
   // top-down, 0..255, soft over about one pixel at the edge.
   bool Decode(uint64_t key, const Prompt& prompt, std::vector<uint8_t>& out, std::string& err);

   // Main thread, once a frame: unloads the models after 60 s idle.
   void Tick(double nowSeconds);
   // Drops the models and the cache now (tests, Settings > Extensions remove).
   void Unload();
}
