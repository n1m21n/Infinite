// Patch file watching, unsaved-changes guard (moved verbatim from main.cpp).
#include "app/AppShared.h"

namespace app
{
   // Resolves a bundled asset shipped next to the app (fonts, icon fonts) from
   // the *executable's own location*, not the process's working directory.
   //
   // This matters because of two different launch contexts that disagree about
   // cwd: a Finder/LaunchServices launch on macOS leaves cwd at
   // Contents/Resources (Cocoa's doing), but the run-infinite-hygiene harness
   // (and any other direct exec, e.g. from a terminal or a debugger) invokes
   // the binary directly with whatever cwd the caller happened to have - it is
   // never Resources there. Resolving relative to argv[0]/getcwd would work by
   // accident in one context and silently fail in the other. Platform::
   // ExecutablePath() (Platform.mm's _NSGetExecutablePath /
   // PlatformWin.cpp's GetModuleFileNameW / PlatformLinux.cpp's /proc/self/exe)
   // is the one source of truth for "where is my own binary" across platforms
   // and doesn't depend on cwd at all.
   //
   // `relPath` is relative to the Resources directory: macOS ships assets at
   // Contents/Resources/<relPath> (MACOSX_PACKAGE_LOCATION "Resources/..." in
   // CMakeLists.txt, the same convention the app icons already use); Windows
   // and Linux have no bundle, so CMake's post-build step copies the same assets
   // to Resources/<relPath> next to the Infinite binary. Returns an empty string
   // if the executable path can't be resolved or the file isn't there, so callers
   // can fall through to their own fallback chain.
   std::string BundledResourcePath(const char* relPath)
   {
      const std::string exe = Platform::ExecutablePath();
      if (exe.empty())
         return {};
      // Platform::ExecutablePath is UTF-8 on every platform; u8path reads it as
      // such (a plain path(std::string) on Windows would use the ANSI code page
      // and miss Resources\ under a non-ASCII user or install folder).
      std::filesystem::path exeDir = std::filesystem::u8path(exe).parent_path();
#if defined(__APPLE__)
      // exe is at Contents/MacOS/Infinite -> Resources is a sibling of MacOS.
      std::filesystem::path resourceDir = exeDir.parent_path() / "Resources";
#else
      // No bundle on Windows/Linux: Resources sits next to Infinite binary.
      std::filesystem::path resourceDir = exeDir / "Resources";
#endif
      std::filesystem::path full = resourceDir / relPath;
      std::error_code ec;
      if (!std::filesystem::exists(full, ec))
         return {};
      return full.u8string(); // UTF-8 back out, same convention as ExecutablePath
   }


   void SetWindowIcon(GLFWwindow* window)
   {
      if (!window) return;
#if defined(_WIN32)
      Platform::SetWindowIconFromResource(window);
#elif !defined(__APPLE__)
      const std::string iconPath = BundledResourcePath("icons/icon_1024.png");
      if (iconPath.empty()) return;

      // The single STB_IMAGE_IMPLEMENTATION in this target (EnvironmentNode.cpp)
      // is compiled with STBI_NO_STDIO, so the path-based stbi_load does not
      // exist to link against - read the file and decode from memory, the same
      // way MediaDecodePortable does.
      //
      // Plain std::ifstream, not the house OpenIfstreamUtf8: this branch is
      // Linux-only (Windows takes SetWindowIconFromResource above, macOS takes
      // neither), and on Linux a path is already UTF-8 bytes so there is
      // nothing to convert. Reaching for the portable helper here would mean
      // including platform/common/PathOpen.h, which pulls WinCommon.h and so
      // <windows.h> into main.cpp - and wingdi.h declares a FUNCTION named
      // Polyline, which hides core/Mesh.h's global `struct Polyline` and breaks
      // every MeshOps declaration that takes one. main.cpp includes no Windows
      // headers at all today; keep it that way.
      std::ifstream iconFile(iconPath, std::ios::binary | std::ios::ate);
      if (!iconFile) return;
      const std::streamoff iconSize = iconFile.tellg();
      if (iconSize <= 0) return;
      std::vector<unsigned char> iconBytes((size_t)iconSize);
      iconFile.seekg(0, std::ios::beg);
      if (!iconFile.read(reinterpret_cast<char*>(iconBytes.data()), iconSize)) return;

      int w = 0, h = 0, channels = 0;
      unsigned char* pixels =
         stbi_load_from_memory(iconBytes.data(), (int)iconBytes.size(), &w, &h, &channels, 4);
      if (pixels)
      {
         GLFWimage img;
         img.width = w;
         img.height = h;
         img.pixels = pixels;
         glfwSetWindowIcon(window, 1, &img);
         stbi_image_free(pixels);
      }
#endif
   }
#if defined(__APPLE__)

   constexpr const char* kPatchExtension = ".inf";
#endif
#if defined(__APPLE__)
#else

   constexpr const char* kPatchExtension = ".infinite";
#endif


   void SavePatchInteractive(bool forceDialog)
   {
      std::string path = gPatchPath;
      if (path.empty() || forceDialog)
      {
         std::string suggested = std::string("Untitled") + kPatchExtension;
         if (!gPatchPath.empty())
         {
            const size_t slash = gPatchPath.find_last_of("/\\");
            suggested = (slash == std::string::npos) ? gPatchPath : gPatchPath.substr(slash + 1);
         }
         path = Platform::SavePatchDialog(suggested);
         if (path.empty())
            return; // cancelled
         // The dialog does not force an extension, and a patch without one is
         // awkward to find again. Either extension counts as already-suffixed
         // so a .inf patch saved on macOS does not become "name.inf.infinite"
         // when re-saved on Windows.
         if (!HasExtension(path, std::vector<std::string> { "inf", "infinite" }))
            path += kPatchExtension;
      }
      SavePatchTo(path);
   }
 // banner is up: the file changed while the canvas had unsaved edits

   PatchFileStamp ReadPatchFileStamp(const std::string& path)
   {
      PatchFileStamp st;
      std::error_code ec;
      const std::filesystem::path fp = std::filesystem::u8path(path);
      const auto t = std::filesystem::last_write_time(fp, ec);
      if (ec)
         return st;
      const auto sz = std::filesystem::file_size(fp, ec);
      if (ec)
         return st;
      st.valid = true;
      st.mtime = (long long)t.time_since_epoch().count();
      st.size = (unsigned long long)sz;
      return st;
   }


   void NotePatchFileStamp(const std::string& path)
   {
      gPatchWatchPath = path;
      gPatchStamp = ReadPatchFileStamp(path);
      gPatchChangedOnDisk = false;
   }


   void ClearPatchWatch()
   {
      gPatchWatchPath.clear();
      gPatchStamp = PatchFileStamp();
      gPatchChangedOnDisk = false;
   }



   // A key's ParamRef.paramIndex on this node instance. The per-type join (gParamJoin) was probed on a
   // default-state node, so it is wrong for any node whose body registers a different set of controls
   // in another state; the live registration is the truth. Matches the control by the address of the
   // member it edits (ParamRef.srcAddr against the address VisitParams hands out for the key), falling
   // back to the join when the control is not drawn right now (a hidden control keeps a stable
   // ordinal, see FixedParamOrdinal).
   namespace
   {
      class KeyAddrFinder : public ParamVisitor
      {
      public:
         explicit KeyAddrFinder(const std::string& k) : key(k) {}
         const void* addr = nullptr;
         void Float(const char* n, float& v) override { if (key == n) addr = &v; }
         void Int(const char* n, int& v) override { if (key == n) addr = &v; }
         void Bool(const char* n, bool& v) override { if (key == n) addr = &v; }
         void Text(const char*, std::string&) override {}
         void Color(const char*, float*) override {}
      private:
         std::string key;
      };
   }

   int ParamIndexOfKeyOnNode(const GraphNode& gn, const std::string& key)
   {
      if (gn.node != nullptr)
      {
         KeyAddrFinder f(key);
         gn.node->VisitParams(f);
         if (f.addr != nullptr)
            for (const ParamRef& r : Modulation::Instance().FrameParams())
               if (r.nodeIndex == gn.index && r.srcAddr == f.addr)
                  return r.paramIndex;
      }
      auto j = gParamJoin.find(gn.typeName);
      if (j == gParamJoin.end())
         return -1;
      auto k = j->second.paramOfKey.find(key);
      return k == j->second.paramOfKey.end() ? -1 : k->second;
   }

   // Resolves the keyed bindings a load had to postpone (see PendingKeyed). Runs once a frame; waits for
   // every destination node to have registered its controls (a node only does that by drawing).
   void PollPendingKeyed()
   {
      if (!gPendingKeyed.active)
         return;
      PendingKeyed& pk = gPendingKeyed;
      auto drawn = [&](int savedIndex)
      {
         auto it = pk.remap.find(savedIndex);
         if (it == pk.remap.end())
            return true; // the node never loaded: nothing to wait for
         for (const ParamRef& r : Modulation::Instance().FrameParams())
            if (r.nodeIndex == it->second)
               return true;
         return false;
      };
      bool ready = true;
      for (const Patch::ModRecord& m : pk.mods)
         ready = ready && drawn(m.dstIndex);
      for (const Patch::ExprRecord& e : pk.exprs)
         ready = ready && drawn(e.dstIndex);
      if (!ready && ++pk.waited < 60)
         return;
      PendingKeyed work = std::move(pk);
      pk = PendingKeyed();
      JoinLiveTier1();
      gHeadlessProbeAll = false;
      int applied = 0;
      std::string missed;
      auto liveOf = [&](int saved) -> GraphNode*
      {
         auto it = work.remap.find(saved);
         return it == work.remap.end() ? nullptr : FindNodeByIndex(it->second);
      };
      auto paramOf = [&](const GraphNode& gn, const std::string& key) -> int
      {
         return ParamIndexOfKeyOnNode(gn, key);
      };
      for (const Patch::ModRecord& m : work.mods)
      {
         GraphNode* dst = liveOf(m.dstIndex);
         GraphNode* src = liveOf(m.srcIndex);
         if (dst == nullptr || src == nullptr)
            continue;
         const int p = paramOf(*dst, m.dstKey);
         if (p < 0)
         {
            missed += (missed.empty() ? "" : ", ") + dst->typeName + "." + m.dstKey;
            continue;
         }
         Modulation::Source source;
         source.nodeIndex = src->index;
         source.outputIndex = m.srcOutput;
         source.polarity = m.polarity;
         source.depth = m.depth;
         source.centre = m.centre;
         source.lo = m.lo;
         source.hi = m.hi;
         source.hasRange = m.hasRange;
         source.enabled = m.enabled;
         source.curve = m.curve;
         Modulation::Instance().RestoreLink(dst->index, p, source);
         applied++;
      }
      for (const Patch::ExprRecord& e : work.exprs)
      {
         GraphNode* dst = liveOf(e.dstIndex);
         if (dst == nullptr)
            continue;
         const int p = paramOf(*dst, e.dstKey);
         if (p < 0)
         {
            missed += (missed.empty() ? "" : ", ") + dst->typeName + "." + e.dstKey;
            continue;
         }
         Modulation::Instance().SetExpression(dst->index, p, e.text);
         if (std::abs(e.curve) > 0.0001f)
            Modulation::Instance().SetExpressionCurve(dst->index, p, e.curve);
         applied++;
      }
      gPatchStatus = std::to_string(applied) + " binding(s) by control name attached" +
                     (missed.empty() ? std::string() : "; not found: " + missed);
   }


   void PollPatchFileWatch(bool force) // force: the self-test has no window clock
   {
      if (gPatchWatchPath.empty() || (!force && HeadlessJobActive()))
         return;
      if (!force)
      {
         const double now = glfwGetTime();
         if (now < gPatchWatchNextPoll)
            return;
         gPatchWatchNextPoll = now + 1.0;
      }
      const PatchFileStamp cur = ReadPatchFileStamp(gPatchWatchPath);
      if (!cur.valid || cur == gPatchStamp)
         return; // gone or unchanged (a deleted file keeps the canvas as is)
      if (gPatchDirty)
      {
         gPatchStamp = cur; // remember it so the banner is raised once per change
         gPatchChangedOnDisk = true;
         return;
      }
      if (!LoadPatchFromImpl(gPatchWatchPath, true))
         gPatchStamp = cur; // a half-written or invalid file: wait for the next change, keep the canvas
   }


   // The single gate every action that would discard the current patch
   // (New, Open, Open Recent, drag-drop, close/quit) should route through:
   // runs `action` immediately if the patch has no unsaved changes,
   // otherwise defers it behind the "Unsaved Changes" modal (Save / Don't
   // Save / Cancel) and runs it only once the user resolves that prompt
   // with something other than Cancel. Dev-harness exits
   // (INFINITE_EXITAFTER, selftest modes, screenshot mode) call
   // glfwSetWindowShouldClose directly and deliberately skip this - a
   // scripted run should never block on a modal nobody can see.
   void GuardUnsavedChanges(std::function<void()> action)
   {
      if (!gPatchDirty)
      {
         action();
         return;
      }
      gPendingUnsavedAction = std::move(action);
      gShowUnsavedChangesModal = true;
   }


   // The single gate every real close path (Quit menu, Cmd+Q, red button)
   // routes through.
   void RequestClose(GLFWwindow* window)
   {
      // GLFW already set shouldClose before invoking the close callback
      // that leads here (see _glfwInputWindowCloseRequest) - undo that so
      // the app stays open until we know whether it's safe to close.
      glfwSetWindowShouldClose(window, GLFW_FALSE);
      GuardUnsavedChanges([window]() { glfwSetWindowShouldClose(window, GLFW_TRUE); });
   }
}
