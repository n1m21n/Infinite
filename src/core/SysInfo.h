#pragma once

// Turbo (from upstream): INFINITE_SYSINFO=1 prints a read-only system report
// (version, GL, audio driver and devices, MIDI ports, VST3 folders and
// blocklist, crash report folder) to stdout and exits. diagnose-windows.bat
// runs it into its log. INFINITE_CRASHTEST=1 crashes on purpose, to check
// that a crash report lands in %LOCALAPPDATA%\Infinite\crash.
struct GLFWwindow;

namespace SysInfo
{
   void PrintAndExit(GLFWwindow* window, const char* version);
   void CrashTest();
}
