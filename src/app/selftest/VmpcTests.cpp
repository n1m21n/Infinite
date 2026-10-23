// VMPC self-test: real clips through the real node, picture read back from its output.
#include "app/AppShared.h"

namespace app
{
// ===================================================== INFINITE_VMPCTEST
// Two 1 s clips (red, blue) on pads 1 and 2. Asserts, on the centre pixel of
// the node's own output: a hit shows its clip, a later hit takes over (cut at
// once, crossfade through a mix), a one shot ends to black, "hold last frame"
// keeps it, a gate release stops the pad, and a loop wraps until hit again.
bool RunVmpcTest()
{
   printf("[VMPC TEST] 16-pad video player\n");
   constexpr int kW = 160, kH = 120, kFps = 30, kFrames = 30;
   int failures = 0;
   auto record = [&](const std::string& path, unsigned char r, unsigned char g, unsigned char b) -> bool
   {
      std::remove(path.c_str());
      std::string error;
      Platform::RecorderHandle* rec = Platform::RecorderStart(path, kW, kH, kFps, error);
      if (rec == nullptr)
         return false;
      Platform::RecorderSetInputIsBgra(rec, false); // the fixture writes RGBA
      for (int i = 0; i < kFrames; i++)
      {
         for (int spin = 0; spin < 20000 && Platform::RecorderPendingFrameCount(rec) >= 3; spin++)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
         std::vector<unsigned char> px = Platform::RecorderAcquireFrameBuffer(rec);
         px.resize((size_t)kW * kH * 4);
         for (size_t k = 0; k < px.size(); k += 4)
         {
            px[k] = r; px[k + 1] = g; px[k + 2] = b; px[k + 3] = 255;
         }
         Platform::RecorderAppend(rec, std::move(px), 1);
      }
      int wrote = 0;
      return Platform::RecorderStop(rec, error, &wrote, nullptr) && wrote == kFrames;
   };
   const std::string red = TmpPath("infinite_vmpctest_red.mp4");
   const std::string blue = TmpPath("infinite_vmpctest_blue.mp4");
   if (!record(red, 220, 30, 30) || !record(blue, 30, 30, 220))
   {
      printf("  [FAIL] could not record the fixture clips\nVMPCTEST FAIL\n");
      return false;
   }

   auto node = std::make_unique<VmpcNode>();
   VmpcNode* n = node.get();
   if (!n->LoadPad(0, red) || !n->LoadPad(1, blue))
   {
      printf("  [FAIL] LoadPad refused the fixture clips\nVMPCTEST FAIL\n");
      return false;
   }
   n->SetTestClock(1.0 / kFps);
   int frame = 1000;
   unsigned int scratch = 0;
   auto cook = [&](int count) { for (int i = 0; i < count; i++) n->CookIfNeeded(++frame); };
   auto hit = [&](int pad) { n->SetPadHeld(pad, true); cook(1); n->SetPadHeld(pad, false); };
   auto centre = [&](float rgb[3])
   {
      rgb[0] = rgb[1] = rgb[2] = -1.0f;
      std::vector<float> px;
      const int w = n->GetOutputWidth(), h = n->GetOutputHeight();
      if (w <= 0 || h <= 0 || !GLUtil::ReadTexturePixels(scratch, n->GetOutputTexture(), w, h, px))
         return;
      const size_t i = ((size_t)(h / 2) * w + w / 2) * 4;
      for (int c = 0; c < 3; c++)
         rgb[c] = px[i + c];
   };
   auto check = [&](bool ok, const char* what, const float rgb[3])
   {
      printf("  [%s] %s (centre %.2f %.2f %.2f)\n", ok ? "PASS" : "FAIL", what, rgb[0], rgb[1], rgb[2]);
      if (!ok)
         failures++;
   };
   float c[3];

   n->transition = VmpcNode::kCut;
   hit(0);
   cook(3);
   centre(c);
   check(c[0] > 0.6f && c[2] < 0.35f, "pad 1 hit shows the red clip", c);

   hit(1);
   cook(2);
   centre(c);
   check(c[2] > 0.6f && c[0] < 0.35f, "pad 2 hit cuts to the blue clip", c);

   n->transition = VmpcNode::kCrossfade;
   n->transitionMs = 400.0f;
   hit(0);
   cook(5); // ~200 ms of 400: half way
   centre(c);
   check(c[0] > 0.25f && c[0] < 0.75f && c[2] > 0.25f && c[2] < 0.75f, "crossfade to pad 1 passes through a mix", c);
   cook(15);
   centre(c);
   check(c[0] > 0.6f && c[2] < 0.35f, "crossfade lands on the red clip", c);

   n->transition = VmpcNode::kCut;
   cook(40); // past both clips' ends
   centre(c);
   check(!n->PadPlaying(0) && !n->PadPlaying(1) && c[0] < 0.1f && c[1] < 0.1f && c[2] < 0.1f,
         "one shots end, picture goes to black", c);

   n->holdLast = true;
   hit(1);
   cook(40);
   centre(c);
   check(!n->PadPlaying(1) && c[2] > 0.6f, "hold last frame keeps the ended clip", c);
   n->holdLast = false;

   n->padMode[0] = VmpcNode::kGate;
   n->SetPadHeld(0, true);
   cook(3);
   const bool gateOn = n->PadPlaying(0);
   n->SetPadHeld(0, false);
   cook(2);
   centre(c);
   check(gateOn && !n->PadPlaying(0), "gate plays while held and stops on release", c);

   n->padMode[0] = VmpcNode::kLoopToggle;
   hit(0);
   cook(45); // 1.5 passes of a 1 s clip
   centre(c);
   const bool looping = n->PadPlaying(0) && c[0] > 0.6f;
   hit(0);
   cook(2);
   check(looping && !n->PadPlaying(0), "loop wraps past the clip end, a second hit stops it", c);

   if (scratch != 0)
      glDeleteFramebuffers(1, &scratch);
   node.reset();
   std::remove(red.c_str());
   std::remove(blue.c_str());
   printf(failures == 0 ? "VMPCTEST OK\n" : "VMPCTEST FAIL - %d check(s)\n", failures);
   return failures == 0;
}
}
