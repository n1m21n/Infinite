// Platform / patch-layout self-tests (moved verbatim from main.cpp).
#include "app/AppShared.h"
#if defined(__linux__)
#include "platform/linux/HostEnvironmentLinux.h"
#endif

namespace app
{
// ===================================================== INFINITE_CAMERACONVTEST
#if defined(__linux__)
} // namespace app (the test hook lives in the global Platform namespace)
namespace Platform { namespace CameraLinuxTest {
   // Declared in CameraLinux.cpp, exposed only for this test - see the file
   // comment there ("Synthetic-buffer self-test").
   void YuyvToRgbaForTest(const unsigned char* yuyv, int width, int height,
                          std::vector<unsigned char>& outRgba);
   bool MjpegToRgbaForTest(const unsigned char* data, size_t size, int expectedWidth, int expectedHeight,
                           std::vector<unsigned char>& outRgba);
} }
namespace app
{

// Exercises CameraLinux.cpp's YUYV->RGBA and MJPEG->RGBA converters on
// hand-built buffers, since neither CI nor any container here has a real
// V4L2 camera device to capture from.
int RunCameraConvTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   bool ok = true;

   // --- YUYV -> RGBA -------------------------------------------------------
   // A 2x2 frame (one packed row of two YUYV pixel pairs, Y0 U Y1 V) built
   // from BT.601 values approximating a solid, saturated red.
   {
      const int w = 2, h = 2;
      const unsigned char yuyv[] = {
         76, 84, 76, 255,   // row 0: Y0 U Y1 V
         76, 84, 76, 255    // row 1
      };
      std::vector<unsigned char> rgba;
      Platform::CameraLinuxTest::YuyvToRgbaForTest(yuyv, w, h, rgba);
      const bool sizeOk = rgba.size() == (size_t)w * h * 4;
      bool colorOk = false;
      if (sizeOk)
      {
         // Loose bounds - the point is that the converter ran and produced
         // a strongly red, weakly green/blue opaque pixel, not an exact
         // BT.601 rounding match.
         const unsigned char r = rgba[0], g = rgba[1], b = rgba[2], a = rgba[3];
         colorOk = r > 150 && g < 100 && b < 100 && a == 255;
         printf("yuyv->rgba: r=%d g=%d b=%d a=%d\n", r, g, b, a);
      }
      printf("%s\n", (sizeOk && colorOk) ? "CAMERACONVTEST YUYV OK" : "CAMERACONVTEST YUYV FAIL - BUG");
      ok = ok && sizeOk && colorOk;
   }

   // --- MJPEG -> RGBA -------------------------------------------------------
   // Encodes a small synthetic solid-color image to a real in-memory JPEG
   // with stb_image_write, then decodes it back through the exact converter
   // CameraLinux.cpp's capture thread uses for MJPEG-format webcams.
   {
      const int w = 8, h = 8;
      std::vector<unsigned char> rgb((size_t)w * h * 3);
      for (size_t i = 0; i < rgb.size(); i += 3)
      {
         rgb[i + 0] = 32;
         rgb[i + 1] = 200;
         rgb[i + 2] = 32;
      }
      std::vector<unsigned char> jpeg;
      auto writeFn = [](void* context, void* data, int size) {
         auto* out = static_cast<std::vector<unsigned char>*>(context);
         const unsigned char* bytes = static_cast<const unsigned char*>(data);
         out->insert(out->end(), bytes, bytes + size);
      };
      stbi_write_jpg_to_func(writeFn, &jpeg, w, h, 3, rgb.data(), 90);

      std::vector<unsigned char> rgba;
      const bool decoded = !jpeg.empty() &&
         Platform::CameraLinuxTest::MjpegToRgbaForTest(jpeg.data(), jpeg.size(), w, h, rgba);
      bool colorOk = false;
      if (decoded && rgba.size() == (size_t)w * h * 4)
      {
         const unsigned char r = rgba[0], g = rgba[1], b = rgba[2];
         // JPEG is lossy - allow generous slack around the source color.
         colorOk = std::abs((int)r - 32) < 40 && std::abs((int)g - 200) < 40 && std::abs((int)b - 32) < 40;
         printf("mjpeg->rgba: r=%d g=%d b=%d\n", r, g, b);
      }
      printf("%s\n", (decoded && colorOk) ? "CAMERACONVTEST MJPEG OK" : "CAMERACONVTEST MJPEG FAIL - BUG");
      ok = ok && decoded && colorOk;
   }

   printf("%s\n", ok ? "CAMERACONVTEST OK" : "CAMERACONVTEST FAIL - BUG");
   return ok ? 0 : 1;
}

int RunHostEnvTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   bool ok = true;

   // 1. With APPIMAGE_ORIGINAL_LD_LIBRARY_PATH non-empty:
   {
      setenv("LD_LIBRARY_PATH", "/tmp/.mount_Inf123/usr/lib:/orig/path", 1);
      setenv("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH", "/orig/path", 1);
      {
         ::Platform::ScopedHostEnvironment env;
         const char* val = getenv("LD_LIBRARY_PATH");
         const bool match = val && std::string(val) == "/orig/path";
         if (!match) ok = false;
         printf("  hostenv test 1 (restore original non-empty): %s\n", match ? "OK" : "FAIL");
      }
      const char* restored = getenv("LD_LIBRARY_PATH");
      const bool restOk = restored && std::string(restored) == "/tmp/.mount_Inf123/usr/lib:/orig/path";
      if (!restOk) ok = false;
      printf("  hostenv test 1 restored: %s\n", restOk ? "OK" : "FAIL");
   }

   // 2. With APPIMAGE_ORIGINAL_LD_LIBRARY_PATH empty:
   {
      setenv("LD_LIBRARY_PATH", "/tmp/.mount_Inf123/usr/lib", 1);
      setenv("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH", "", 1);
      {
         ::Platform::ScopedHostEnvironment env;
         const char* val = getenv("LD_LIBRARY_PATH");
         const bool match = (val == nullptr);
         if (!match) ok = false;
         printf("  hostenv test 2 (unset on empty original): %s\n", match ? "OK" : "FAIL");
      }
      const char* restored = getenv("LD_LIBRARY_PATH");
      const bool restOk = restored && std::string(restored) == "/tmp/.mount_Inf123/usr/lib";
      if (!restOk) ok = false;
      printf("  hostenv test 2 restored: %s\n", restOk ? "OK" : "FAIL");
   }

   // 3. Fallback: strip APPDIR when APPIMAGE_ORIGINAL_LD_LIBRARY_PATH is unset
   {
      unsetenv("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH");
      setenv("APPDIR", "/tmp/.mount_InfABC", 1);
      setenv("LD_LIBRARY_PATH", "/tmp/.mount_InfABC/usr/lib:/usr/local/cuda/lib", 1);
      {
         ::Platform::ScopedHostEnvironment env;
         const char* val = getenv("LD_LIBRARY_PATH");
         const bool match = val && std::string(val) == "/usr/local/cuda/lib";
         if (!match) ok = false;
         printf("  hostenv test 3 (strip APPDIR fallback): %s\n", match ? "OK" : "FAIL");
      }
      const char* restored = getenv("LD_LIBRARY_PATH");
      const bool restOk = restored && std::string(restored) == "/tmp/.mount_InfABC/usr/lib:/usr/local/cuda/lib";
      if (!restOk) ok = false;
      printf("  hostenv test 3 restored: %s\n", restOk ? "OK" : "FAIL");
   }

   // 4. Fallback: all entries belong to AppImage
   {
      unsetenv("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH");
      setenv("APPDIR", "/tmp/.mount_InfABC", 1);
      setenv("LD_LIBRARY_PATH", "/tmp/.mount_InfABC/usr/lib:/tmp/.mount_InfABC/lib", 1);
      {
         ::Platform::ScopedHostEnvironment env;
         const char* val = getenv("LD_LIBRARY_PATH");
         const bool match = (val == nullptr);
         if (!match) ok = false;
         printf("  hostenv test 4 (strip all AppImage paths): %s\n", match ? "OK" : "FAIL");
      }
      const char* restored = getenv("LD_LIBRARY_PATH");
      const bool restOk = restored && std::string(restored) == "/tmp/.mount_InfABC/usr/lib:/tmp/.mount_InfABC/lib";
      if (!restOk) ok = false;
      printf("  hostenv test 4 restored: %s\n", restOk ? "OK" : "FAIL");
   }

   // 5. Clean start: LD_LIBRARY_PATH unset
   {
      unsetenv("APPIMAGE_ORIGINAL_LD_LIBRARY_PATH");
      unsetenv("APPDIR");
      unsetenv("LD_LIBRARY_PATH");
      {
         ::Platform::ScopedHostEnvironment env;
         const char* val = getenv("LD_LIBRARY_PATH");
         const bool match = (val == nullptr);
         if (!match) ok = false;
         printf("  hostenv test 5 (originally unset remains unset): %s\n", match ? "OK" : "FAIL");
      }
      const char* restored = getenv("LD_LIBRARY_PATH");
      const bool restOk = (restored == nullptr);
      if (!restOk) ok = false;
      printf("  hostenv test 5 restored: %s\n", restOk ? "OK" : "FAIL");
   }

   printf("%s\n", ok ? "HOSTENVTEST OK" : "HOSTENVTEST FAIL - BUG");
   return ok ? 0 : 1;
}
#endif // __linux__

// ==================================================== INFINITE_SYPHONPATCHTEST
int RunSyphonPatchTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   RegisterNodes();

   Patch::Data data;
   Patch::NodeRecord n1;
   n1.index = 1;
   n1.category = "Utility";
   n1.typeName = "Syphon In";
   n1.x = 100.0f;
   n1.y = 100.0f;
   data.nodes.push_back(n1);

   Patch::NodeRecord n2;
   n2.index = 2;
   n2.category = "Utility";
   n2.typeName = "Syphon Out";
   n2.x = 400.0f;
   n2.y = 100.0f;
   data.nodes.push_back(n2);

   std::string testPath = TmpPath("syphon_test_load.inf");
   std::string writeErr, readErr;
   if (!Patch::Write(testPath, data, writeErr))
   {
      printf("SYPHONPATCHTEST FAIL: could not write test patch: %s\n", writeErr.c_str());
      return 1;
   }

   Patch::Data readData;
   if (!Patch::Read(testPath, readData, readErr))
   {
      printf("SYPHONPATCHTEST FAIL: Patch::Read failed: %s\n", readErr.c_str());
      std::remove(testPath.c_str());
      return 1;
   }
   std::remove(testPath.c_str());

   if (readData.nodes.size() != 2)
   {
      printf("SYPHONPATCHTEST FAIL: expected 2 nodes, got %zu\n", readData.nodes.size());
      return 1;
   }

   // Verify ApplyPatchData instantiates both nodes safely
   NewPatch();
   ApplyPatchData(readData);

   bool foundIn = false;
   bool foundOut = false;
   for (const auto& gn : gNodes)
   {
      if (gn.node && gn.typeName == "Syphon In") foundIn = true;
      if (gn.node && gn.typeName == "Syphon Out") foundOut = true;
   }

   if (!foundIn || !foundOut)
   {
      printf("SYPHONPATCHTEST FAIL: Syphon nodes missing after ApplyPatchData (in=%d, out=%d)\n",
             foundIn ? 1 : 0, foundOut ? 1 : 0);
      return 1;
   }

   printf("SYPHONPATCHTEST OK\n");
   return 0;
}

// ================================================== INFINITE_PATCHLAYOUTTEST
// Data-level half (headless, before glfwInit): a hand-written patch with no
// `pos` lines reads as hasPos=false, PatchLayout places it without overlap in
// band/wiring order, and --canonicalize's Read->Write is unchanged by all of it
// (a pos-less node is still written `pos 0 0`). The live half - real
// ed::GetNodeSize measurements through the GUI tick - is the frame test
// INFINITE_PATCHLAYOUTLIVETEST in the main loop.
static const char* kPatchLayoutFixture =
   "infinite-patch 1\n"
   "# band Picture\n"
   "node 9 Compositing Comment\n"
   "  f width 400\n"
   "  f height 100\n"
   "end\n"
   "node 1 Source Shape\n"
   "  id shape\n"
   "end\n"
   "# near blur\n"
   "node 8 Compositing Comment\n"
   "end\n"
   "node 2 Effects Blur\n"
   "  id blur\n"
   "end\n"
   "node 3 Utility Output\n"
   "  id out\n"
   "end\n"
   "node 4 Modulators LFO\n"
   "end\n"
   "node 5 Modulators LFO\n"
   "end\n"
   "node 6 Notes Arpeggiator\n"
   "end\n"
   "node 7 Synths Analog\n"
   "end\n"
   "cable 2 0 1\n"
   "cable 3 0 2\n"
   "note 7 0 6\n";

int RunPatchLayoutTest()
{
   setvbuf(stdout, nullptr, _IONBF, 0);
   int fails = 0;
   auto check = [&](bool ok, const char* what)
   {
      if (!ok)
      {
         printf("PATCHLAYOUTTEST FAIL: %s\n", what);
         fails++;
      }
   };

   const std::string src = TmpPath("infinite_patchlayout_in.inf");
   const std::string rt = TmpPath("infinite_patchlayout_rt.inf");
   {
      std::ofstream f(src);
      f << kPatchLayoutFixture;
   }
   Patch::Data data;
   std::string err;
   if (!Patch::Read(src, data, err))
   {
      printf("PATCHLAYOUTTEST FAIL: Read: %s\n", err.c_str());
      return 1;
   }
   check(data.nodes.size() == 9, "fixture should read 9 nodes");
   bool allNoPos = true;
   for (const Patch::NodeRecord& n : data.nodes)
      allNoPos = allNoPos && !n.hasPos && n.x == 0.0f && n.y == 0.0f;
   check(allNoPos, "pos-less nodes must read hasPos=false at 0,0 (reader must not invent positions)");
   check(PatchLayout::NeedsLayout(data), "NeedsLayout must be true for an all-pos-less patch");

   auto rec = [&](int index) -> const Patch::NodeRecord&
   {
      for (const Patch::NodeRecord& n : data.nodes)
         if (n.index == index)
            return n;
      return data.nodes.front();
   };
   check(rec(9).layoutHint == "band Picture" && rec(8).layoutHint == "near blur" && rec(1).layoutHint.empty(),
         "# band / # near hints must attach to the next node block only");

   // --canonicalize stability: Read -> Write is byte-identical to a pre-layout
   // build, i.e. pos-less nodes still come out as `pos 0 0`.
   {
      Patch::Data copy = data;
      check(Patch::Write(rt, copy, err), "Write of the pos-less data");
      std::ifstream f(rt);
      std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      size_t zeros = 0, at = 0;
      while ((at = text.find("  pos 0 0\n", at)) != std::string::npos) { zeros++; at += 1; }
      check(zeros == 9, "canonicalize must keep writing `pos 0 0` for pos-less nodes");
      check(PatchLayout::NeedsLayout(data), "Write must not consume or alter the pos-less state");
   }

   const std::map<int, PatchLayout::Pos> pos = PatchLayout::Compute(data);
   check(pos.size() == 9, "every node gets a position");

   // No two rectangles overlap.
   int overlaps = 0;
   for (const Patch::NodeRecord& a : data.nodes)
      for (const Patch::NodeRecord& b : data.nodes)
      {
         if (a.index >= b.index)
            continue;
         const PatchLayout::Size sa = PatchLayout::EstimateSize(a), sb = PatchLayout::EstimateSize(b);
         const PatchLayout::Pos pa = pos.at(a.index), pb = pos.at(b.index);
         if (pa.x < pb.x + sb.w && pb.x < pa.x + sa.w && pa.y < pb.y + sb.h && pb.y < pa.y + sa.h)
            overlaps++;
      }
   check(overlaps == 0, "no two nodes may overlap");

   check(pos.at(1).x < pos.at(2).x && pos.at(2).x < pos.at(3).x, "picture chain runs left to right by wiring depth");
   check(pos.at(6).x < pos.at(7).x, "arpeggiator sits left of the synth it feeds");
   check(pos.at(1).y < pos.at(7).y && pos.at(7).y < pos.at(4).y, "bands stack Picture, Sound, Modulation");
   check(pos.at(4).y == pos.at(5).y && pos.at(4).x < pos.at(5).x, "modulators lay out as one row");
   check(pos.at(8).x == pos.at(2).x && pos.at(8).y + 100.0f < pos.at(2).y, "`# near blur` comment sits above blur");
   check(pos.at(9).y < pos.at(1).y, "`# band Picture` comment heads the band");

   // A measured size beats the estimate: widen the shape and its column
   // neighbour must move right by the difference.
   {
      std::map<int, PatchLayout::Size> measured;
      measured[1] = { 2000.0f, 300.0f };
      const std::map<int, PatchLayout::Pos> wide = PatchLayout::Compute(data, &measured);
      check(wide.at(2).x >= pos.at(2).x + 1600.0f, "a measured width must push the next column right");
      measured[1] = { 0.0f, 0.0f };
      const std::map<int, PatchLayout::Pos> same = PatchLayout::Compute(data, &measured);
      check(same.at(2).x == pos.at(2).x, "a zero measurement must fall back to the estimate");
   }

   // Apply -> save -> load keeps the layout and is then never re-laid-out.
   {
      Patch::Data placed = data;
      check(PatchLayout::Apply(placed), "Apply on a pos-less patch");
      check(!PatchLayout::NeedsLayout(placed), "Apply marks every node hasPos");
      check(!PatchLayout::Apply(placed), "Apply is a no-op the second time");
      check(Patch::Write(rt, placed, err), "Write of the laid-out data");
      Patch::Data back;
      check(Patch::Read(rt, back, err), "Read of the laid-out file");
      check(!PatchLayout::NeedsLayout(back), "a saved layout reads back with pos");
      bool same = back.nodes.size() == placed.nodes.size();
      for (size_t i = 0; same && i < back.nodes.size(); ++i)
         same = std::fabs(back.nodes[i].x - placed.nodes[i].x) < 0.01f && std::fabs(back.nodes[i].y - placed.nodes[i].y) < 0.01f;
      check(same, "positions survive Write -> Read");
   }

   // One node with a pos line means the author placed things: leave it alone.
   {
      Patch::Data mixed = data;
      mixed.nodes[1].hasPos = true;
      check(!PatchLayout::NeedsLayout(mixed), "a patch with any pos record is never auto-laid-out");
      check(!PatchLayout::NeedsLayout(Patch::Data()), "an empty patch needs no layout");
   }
   // GUI-built records (undo snapshots, copy/paste) default to hasPos.
   check(Patch::NodeRecord().hasPos, "NodeRecord defaults to hasPos so GUI snapshots are never re-laid-out");

   std::remove(src.c_str());
   std::remove(rt.c_str());
   if (fails == 0)
      printf("PATCHLAYOUTTEST OK\n");
   return fails == 0 ? 0 : 1;
}
}
