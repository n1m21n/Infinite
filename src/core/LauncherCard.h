#pragma once

#include "SplashScreen.h"

#include <GLFW/glfw3.h>
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <string>

// Plays the launcher card (SplashScreen.h) in its own small borderless window, on a private ImGui
// context, while the main window stays hidden. The caller shows the main window afterwards.
// Returns false if the card window could not be created. Leaves mainWin's GL context current.
// Callers re-apply their own window hints afterwards: this resets them to GLFW defaults.
namespace LauncherCard
{
   // Set at window creation: the main window is created hidden and the card plays first.
   inline bool& Enabled()
   {
      static bool e = false;
      return e;
   }

   inline bool Run(GLFWwindow* mainWin, const std::string& fontPath)
   {
      const int cw = 600, ch = 380;
      glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
      glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
      glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
      glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
      glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
      GLFWwindow* card = glfwCreateWindow(cw, ch, "Infinite", nullptr, mainWin);
      glfwDefaultWindowHints();
      glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
      glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
      glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
      glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
      glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
      if (!card)
         return false;
      if (GLFWmonitor* mon = glfwGetPrimaryMonitor())
      {
         int mx = 0, my = 0, mw = 0, mh = 0;
         glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
         if (mw > 0 && mh > 0)
            glfwSetWindowPos(card, mx + (mw - cw) / 2, my + (mh - ch) / 2);
      }
      glfwMakeContextCurrent(card);
      glfwSwapInterval(1);

      ImGuiContext* mainCtx = ImGui::GetCurrentContext();
      ImGuiContext* ctx = ImGui::CreateContext();
      ImGui::SetCurrentContext(ctx);
      ImGuiIO& io = ImGui::GetIO();
      io.IniFilename = nullptr;
      io.LogFilename = nullptr;
      ImFont* font = fontPath.empty() ? nullptr : io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 32.0f);
      if (!font)
         io.Fonts->AddFontDefault();
      ImGui_ImplOpenGL3_Init("#version 150");

      glfwShowWindow(card);
      Splash::Begin(0.0f, true);
      double last = glfwGetTime();
      while (Splash::Active() && !glfwWindowShouldClose(card))
      {
         glfwPollEvents();
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
      ImGui_ImplOpenGL3_Shutdown();
      ImGui::DestroyContext(ctx);
      ImGui::SetCurrentContext(mainCtx);
      glfwDestroyWindow(card);
      glfwMakeContextCurrent(mainWin);
      return true;
   }
} // namespace LauncherCard
