// Split out of main(): see docs/plans/main-split/README.md (Block C)
#include "app/frame/FrameCtx.h"

namespace app
{
void DrawPresent(FrameCtx& fc)
{
   auto& window = fc.window;
   auto& frameId = fc.frameId;
   auto& now = fc.now;

      // The projectors timer above has closed, so this frame's record is complete.
      if (sTailProjIntervalMs > 0.0)
         Bench::Tail().EndFrame(sTailProjIntervalMs);
      sTailProjIntervalMs = -1.0;

      GLUtil::EndFrameScratchFbos();
      ++frameId;
      // The uid map is rebuilt at most once a frame on first use (WP5b), so a
      // gNodes mutation that did not report in is stale for one frame at most
      // - and FindNodeByUid's own storage/size check catches most of those.
      InvalidateNodeByUid();

      // Patch shortcuts. Handled outside the node editor so its own Cmd/Ctrl-key
      // bindings do not swallow them, and gated on no text field having focus
      // so typing an 'S' into a Text node does not save the patch. KeySuper
      // alone would miss Windows, where Cmd doesn't exist and Ctrl reports as
      // KeyCtrl, not KeySuper (that's the Windows key there).
      if (!ImGui::GetIO().WantTextInput && (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl))
      {
         if (ImGui::IsKeyPressed(ImGuiKey_0, false))
            gSettingsOpen = true;
         else if (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_Minus, true))
         {
            // UI scale in 0.1 steps, like a browser's zoom. Applied through the same rescale path as the Settings slider.
            const bool up = ImGui::IsKeyPressed(ImGuiKey_Equal, true);
            const float next = std::clamp(std::round((CategoryColors::GetUiScale() + (up ? 0.1f : -0.1f)) * 10.0f) / 10.0f, 0.5f, 2.0f);
            if (next != CategoryColors::GetUiScale())
            {
               CategoryColors::SetUiScale(next);
               UiScale::RequestRescale();
            }
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            SavePatchInteractive(ImGui::GetIO().KeyShift);
         else if (ImGui::IsKeyPressed(ImGuiKey_O, false))
         {
            const std::string path = Platform::OpenPatchDialog();
            if (!path.empty())
               GuardUnsavedChanges([path]() { LoadPatchFrom(path); });
         }
         else if (ImGui::IsKeyPressed(ImGuiKey_N, false))
            GuardUnsavedChanges([]() { NewPatch(); });
      }

      // Frame limiter. Sleeping most of the way there and spinning the last
      // sliver keeps the cap accurate without burning a core: sleep_for is only
      // accurate to a millisecond or two, which at 120fps is most of the budget.
      // Skipped entirely when Vsync is on: glfwSwapBuffers above already paced
      // this frame to the display's refresh, and topping that up against a
      // second, unrelated budget just fights the vsync quantization instead of
      // capping anything more precisely (the Target FPS control is disabled in
      // the UI whenever Vsync is on, for the same reason). Skipped too while a
      // projector window paces the loop to its display's refresh.
      if (gTargetFps > 0 && !gVsync && !FrameClockActive())
      {
         const double budget = 1.0 / (double)gTargetFps;
         const double deadline = gFrameStart + budget;
         if (Platform::AnyPluginEditorOpen())
         {
            // Spend the idle slack servicing the editor's run loop instead of
            // sleeping through it - see local-prompts/02-plugin-editor-lag.md.
            while (glfwGetTime() < deadline)
               if (!Platform::PumpPluginEditorEvents())
                  std::this_thread::sleep_for(std::chrono::microseconds(200));
         }
         else
         {
            const double slack = deadline - glfwGetTime();
            if (slack > 0.002)
               std::this_thread::sleep_for(std::chrono::duration<double>(slack - 0.001));
            while (glfwGetTime() < deadline)
               std::this_thread::yield();
         }
      }

      {
         // Measured after the swap so the number includes GPU work the driver
         // blocks on there, which is where a heavy 3D render actually lands.
         static double sPrevTime = 0.0;
         const double now = glfwGetTime();
         if (sPrevTime > 0.0)
            gLastFrameMs = (now - sPrevTime) * 1000.0;
         sPrevTime = now;
      }

      // Dev harness: quit after N frames. The self-tests above printf their
      // verdict and then run forever, so a scripted run has to kill the app -
      // which throws away stdout still sitting in the block buffer when it is
      // redirected to a file. Exiting normally lets it flush.
      if (const char* exitAfter = getenv("INFINITE_EXITAFTER"))
      {
         if (frameId >= atoi(exitAfter))
         {
            // Drag-test fixtures print their verdict from inside their own
            // phase machine; if the run times out before ever finding a
            // usable row (sPhase stuck at 0), that machine never gets to
            // print anything and the driver silently reads "no FAIL" as a
            // pass. Say so explicitly instead.
            if (getenv("INFINITE_SAMPLERDRAGTEST") != nullptr && gSamplerDragTestPhase == 0)
               printf("SAMPLERDRAGTEST FAIL (timed out waiting for a usable row)\n");
            if (getenv("INFINITE_MEDIADRAGTEST") != nullptr && gMediaDragTestPhase == 0)
               printf("MEDIADRAGTEST FAIL (timed out waiting for a usable row)\n");
            if (getenv("INFINITE_PLUGINDRAGTEST") != nullptr && gPluginDragTestPhase == 0)
            {
               // An empty plugin index isn't a failure of the drag
               // mechanism - it just means this host has nothing installed
               // to drag.
               if (gPluginScanner.Index().empty())
                  printf("PLUGINDRAGTEST SKIP (no plugins installed on this host)\n");
               else
                  printf("PLUGINDRAGTEST FAIL (timed out waiting for a usable row)\n");
            }
            fflush(stdout);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
         }
      }}
}
