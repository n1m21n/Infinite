#pragma once

#include "SplashScreen.h"
#include "../platform/AppPaths.h"

#include <GLFW/glfw3.h>
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>

// Plays the launcher card (SplashScreen.h) in its own small borderless window, on a private ImGui
// context, while the main window stays hidden. The caller shows the main window afterwards.
// Returns false if the card could not play (window not created, or an exception mid-card); the
// caller then falls back to the in-window Splash. Leaves mainWin's GL context current.
//
// Must run BEFORE ImGui_ImplGlfw_InitForOpenGL(mainWin). The GLFW backend's callbacks (focus,
// cursor-enter, char, key, monitor) and, on Windows, its WndProc subclass look up their data
// through the *current* ImGui context; with the card's private context current they find none
// and dereference null as soon as glfwPollEvents delivers anything to the main window. v0.4.7
// played the card after the backend existed and crashed on Windows (0xc000041d).
namespace LauncherCard
{
   // Set at window creation: the main window is created hidden and the card plays first.
   inline bool& Enabled()
   {
      static bool e = false;
      return e;
   }

   // The hints the card overrides, as the main window had them. GLFW hints are sticky global
   // state with no getter, so the caller states them; Run puts them back on every exit path so
   // later windows (projectors) are created exactly as they would have been without the card.
   struct WindowHints
   {
      int decorated = GLFW_TRUE;
      int resizable = GLFW_TRUE;
      int visible = GLFW_TRUE;
      int transparentFramebuffer = GLFW_FALSE;
      int scaleToMonitor = GLFW_TRUE;

      void Apply() const
      {
         glfwWindowHint(GLFW_DECORATED, decorated);
         glfwWindowHint(GLFW_RESIZABLE, resizable);
         glfwWindowHint(GLFW_VISIBLE, visible);
         glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, transparentFramebuffer);
         glfwWindowHint(GLFW_SCALE_TO_MONITOR, scaleToMonitor);
      }
   };

   // Crash sentinel: written before the card plays, removed after. If a start dies inside the
   // card (a fault no try/catch sees), the next start finds it, skips the card once and uses the
   // in-window Splash, so a startup fault can never lock a user out. Fails closed: if the file
   // cannot be written or read, the card simply plays.
   inline std::string SentinelPath(const std::string& dir)
   {
      return dir.empty() ? std::string() : dir + "/launcher_card_running";
   }

   inline void MarkRunning(const std::string& dir)
   {
      const std::string p = SentinelPath(dir);
      if (!p.empty())
         std::ofstream(AppPaths::FsPath(p)) << "1\n";
   }

   inline void ClearRunning(const std::string& dir)
   {
      const std::string p = SentinelPath(dir);
      std::error_code ec;
      if (!p.empty())
         std::filesystem::remove(AppPaths::FsPath(p), ec);
   }

   // True when the previous start never got past the card. Consumes the sentinel, so the card
   // is skipped exactly once.
   inline bool ConsumeStaleSentinel(const std::string& dir)
   {
      const std::string p = SentinelPath(dir);
      std::error_code ec;
      if (p.empty() || !std::filesystem::exists(AppPaths::FsPath(p), ec))
         return false;
      std::filesystem::remove(AppPaths::FsPath(p), ec);
      return true;
   }

   // startAt / onFrame are test hooks: start the card that many seconds in, and call onFrame once
   // per card frame right after glfwPollEvents.
   inline bool Run(GLFWwindow* mainWin, const std::string& fontPath, const WindowHints& mainHints,
                   float startAt = 0.0f, const std::function<void()>& onFrame = {})
   {
      const int cw = 600, ch = 380;
      glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
      glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
      glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
      glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
      GLFWwindow* card = glfwCreateWindow(cw, ch, "Infinite", nullptr, mainWin);
      mainHints.Apply();
      if (!card)
         return false;

      ImGuiContext* mainCtx = ImGui::GetCurrentContext();
      ImGuiContext* ctx = nullptr;
      bool rendererUp = false;
      bool played = true;
      try
      {
         if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
         {
            int mx = 0, my = 0, mw = 0, mh = 0;
            glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
            if (mw > 0 && mh > 0)
               glfwSetWindowPos(card, mx + (mw - cw) / 2, my + (mh - ch) / 2);
         }
         glfwMakeContextCurrent(card);
         glfwSwapInterval(1);

         ctx = ImGui::CreateContext();
         ImGui::SetCurrentContext(ctx);
         ImGuiIO& io = ImGui::GetIO();
         io.IniFilename = nullptr;
         io.LogFilename = nullptr;
         ImFont* font = fontPath.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 32.0f);
         if (!font)
            io.Fonts->AddFontDefault();
         rendererUp = ImGui_ImplOpenGL3_Init("#version 150");

         glfwShowWindow(card);
         Splash::Begin(startAt, true);
         double last = glfwGetTime();
         while (Splash::Active() && !glfwWindowShouldClose(card))
         {
            glfwPollEvents();
            if (onFrame)
               onFrame();
            int ww = 0, wh = 0, fw = 0, fh = 0;
            glfwGetWindowSize(card, &ww, &wh);
            glfwGetFramebufferSize(card, &fw, &fh);
            const double now = glfwGetTime();
            io.DeltaTime = (float)std::max(1.0e-4, now - last);
            last = now;
            io.DisplaySize = ImVec2((float)ww, (float)wh);
            io.DisplayFramebufferScale = ImVec2(ww > 0 ? (float)fw / (float)ww : 1.0f, wh > 0 ? (float)fh / (float)wh : 1.0f);
            double mxp = 0.0, myp = 0.0;
            glfwGetCursorPos(card, &mxp, &myp);
            io.AddMousePosEvent((float)mxp, (float)myp);
            io.AddMouseButtonEvent(0, glfwGetMouseButton(card, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
            io.AddMouseButtonEvent(1, glfwGetMouseButton(card, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
            io.AddKeyEvent(ImGuiKey_Escape, glfwGetKey(card, GLFW_KEY_ESCAPE) == GLFW_PRESS);
            io.AddKeyEvent(ImGuiKey_Space, glfwGetKey(card, GLFW_KEY_SPACE) == GLFW_PRESS);
            io.AddKeyEvent(ImGuiKey_Enter, glfwGetKey(card, GLFW_KEY_ENTER) == GLFW_PRESS);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui::NewFrame();
            Splash::Draw();
            ImGui::Render();
            glViewport(0, 0, fw, fh);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(card);
         }
      }
      catch (...)
      {
         played = false;
      }
      // The in-window Splash shares Splash::S(); leave it idle for the caller either way.
      Splash::S() = Splash::State{};
      if (ctx)
      {
         ImGui::SetCurrentContext(ctx);
         if (rendererUp)
            ImGui_ImplOpenGL3_Shutdown();
         ImGui::DestroyContext(ctx);
      }
      ImGui::SetCurrentContext(mainCtx);
      glfwDestroyWindow(card);
      glfwMakeContextCurrent(mainWin);
      return played;
   }
} // namespace LauncherCard

namespace LauncherCard
{
   // Self-test helper (INFINITE_LAUNCHERCARDTEST): calls whatever focus, cursor-enter, char, key
   // and monitor callbacks are installed on the main window right now, as glfwPollEvents would
   // when the OS delivers those events mid-card. Returns how many were installed.
   inline int InjectMainWindowEvents(GLFWwindow* w)
   {
      int n = 0;
      if (GLFWwindowfocusfun f = glfwSetWindowFocusCallback(w, nullptr))
      {
         glfwSetWindowFocusCallback(w, f);
         f(w, GLFW_TRUE);
         ++n;
      }
      if (GLFWcursorenterfun f = glfwSetCursorEnterCallback(w, nullptr))
      {
         glfwSetCursorEnterCallback(w, f);
         f(w, GLFW_TRUE);
         ++n;
      }
      if (GLFWcharfun f = glfwSetCharCallback(w, nullptr))
      {
         glfwSetCharCallback(w, f);
         f(w, 'a');
         ++n;
      }
      if (GLFWkeyfun f = glfwSetKeyCallback(w, nullptr))
      {
         glfwSetKeyCallback(w, f);
         f(w, GLFW_KEY_A, 0, GLFW_PRESS, 0);
         f(w, GLFW_KEY_A, 0, GLFW_RELEASE, 0);
         ++n;
      }
      if (GLFWmonitorfun f = glfwSetMonitorCallback(nullptr))
      {
         glfwSetMonitorCallback(f);
         if (GLFWmonitor* m = glfwGetPrimaryMonitor())
            f(m, GLFW_CONNECTED);
         ++n;
      }
      return n;
   }
} // namespace LauncherCard
