#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "app/frame/FrameCtx.h"
#include "app/frame/FrameTests.h"

namespace app
{


}

using namespace app;


int main(int argc, char** argv)
{
   FrameCtx fc;
   {
      const int rc = InitApp(fc, argc, argv);
      if (rc >= 0)
         return rc;
   }
   auto& window = fc.window;
   while (!glfwWindowShouldClose(window))
   {
      DrawFramePump(fc);
      DrawMenuBar(fc);
      DrawLayout(fc);
      DrawDropHandling(fc);
      DrawFrameTestsA(fc);
      DrawBenchHarness(fc);
      DrawNodeBodies(fc);
      DrawLinks(fc);
      DrawKeyboard(fc);
      DrawPopupsA(fc);
      DrawPopupsB(fc);
      DrawSidePanels(fc);
      {
         const int rc = DrawFloating(fc);
         if (rc >= 0)
            return rc;
      }
      {
         const int rc = DrawTestsB(fc);
         if (rc >= 0)
            return rc;
      }
      DrawPresent(fc);
      fc.timerLinksGpu.reset();
      fc.timerLinks.reset();
      fc.timerNodeBodiesGpu.reset();
      fc.timerNodeBodies.reset();
   }

   // Every real close path (Quit menu, red button, Cmd+Q) routes through
   // RequestClose -> glfwSetWindowShouldClose, and every dev-harness exit
   // sets the same flag directly - so this fall-through is the single place
   // every one of them converges, and the only place a clean exit needs to
   // delete the marker. Same INFINITE_EXITAFTER carve-out as the startup
   // check: a harness run never created the real marker, so it must not
   // delete it either - see UsingAutosaveTestPaths.
   if (getenv("INFINITE_EXITAFTER") == nullptr && !HeadlessJobActive())
   {
      const std::string marker = AutosaveMarkerPath();
      if (!marker.empty())
      {
         std::error_code ec;
         std::filesystem::remove(marker, ec);
      }
   }

   CloseAllProjectorWindows();
   Platform::StopDisplayRefreshClock();
   AudioEngine::Instance().Stop();
   Extensions::Shutdown();
   UpdateCheck::Shutdown(); // joins the worker thread so the process doesn't exit mid-request
   MovementLog::Stop();
   if (ColorStats::Engine::Instance().HasLearnedData())
      ColorStats::Engine::Instance().Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveQuantizeProfile::HasLearnedData())
      PredictiveQuantizeProfile::Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveVelocityProfile::HasLearnedData())
      PredictiveVelocityProfile::Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveNotesStyle::HasLearnedData())
      PredictiveNotesStyle::Save(AppPaths::AppSupportDir() + "/prediction");
   if (PredictiveRhythmStyle::HasLearnedData())
      PredictiveRhythmStyle::Save(AppPaths::AppSupportDir() + "/prediction");
   gNodes.clear();
   if (getenv("INFINITE_RECTEARDOWNTEST") != nullptr && std::string(getenv("INFINITE_RECTEARDOWNTEST")) == "quit")
      printf("quit-mid-record: survived  OK\n");
   ed::DestroyEditor(gEditor);
   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImGui::DestroyContext();
   glfwDestroyWindow(window);
   glfwTerminate();

   // Not `return 0`: that runs the full C++/ObjC static-destructor chain
   // (__cxa_finalize_ranges) before the process dies, which includes global
   // destructors belonging to any third-party plugin framework still mapped
   // into our address space. Seen live: Kilohearts' HeartCore crashes in its
   // own global teardown on ordinary quit (SIGSEGV in a stale objc_msgSend
   // "clear" call, called from exit() via __cxa_finalize_ranges) - nothing
   // we did wrong, just an unload-order bug in code we don't control that
   // has no business running in a process that's about to disappear anyway.
   // Everything we actually need torn down already happened above; _Exit
   // skips straight to process termination without running anyone else's
   // destructors.
   //
   // _Exit also skips the normal atexit-driven stdio flush, unlike exit()/
   // return from main. The INFINITE_EXITAFTER harness path already knew this
   // and fflush(stdout)'d itself before requesting the close - but any
   // self-test that calls glfwSetWindowShouldClose() directly on its own
   // verdict (most of them do, e.g. INFINITE_MINIVIEWPORTTEST) reaches here
   // without ever going through that branch, so its buffered printf verdict
   // silently vanished whenever stdout was redirected to a file (i.e. every
   // driver.sh run). One flush here covers every self-test's exit path
   // instead of requiring each one to remember its own.
   fflush(stdout);
   std::_Exit(gHeadlessExitCode);
}
