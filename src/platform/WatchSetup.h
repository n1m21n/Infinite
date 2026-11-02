#pragma once

#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// "Set up watch app": the checklist behind the Apple Watch node's setup window (docs/plans/apple-watch/README.md,
// D-B2 to D-B5). Everything that touches Xcode or a device sits behind IToolchain, so the selftest fakes it and
// never runs xcodebuild or devicectl. The real toolchain (macOS only) shells out to the system tools and to the
// bundled apps/watch/install.sh.
namespace WatchSetup
{
   struct Team
   {
      std::string id, name;
      bool personal = false;   // free "Personal Team"
   };

   struct Device
   {
      std::string id, name;
      bool isWatch = false;
      bool developerMode = false;
   };

   // What one scan of the machine found.
   struct Probe
   {
      bool supported = true;          // false off macOS
      bool projectBundled = true;     // the xcodeproj is inside Infinite.app (D-B1)
      bool xcodeInstalled = false;
      std::string xcodeVersion;
      std::vector<Team> teams;
      std::vector<Device> devices;
   };

   struct InstallRequest
   {
      std::string teamId;
      std::string deviceId;
   };

   class IToolchain
   {
   public:
      virtual ~IToolchain() = default;
      virtual Probe Scan() = 0;                                     // may block for a second or two
      // Runs the install; `line` gets the STEP / ERROR / FALLBACK / DONE lines as they arrive. True only when installed.
      virtual bool Install(const InstallRequest& req, const std::function<void(const std::string&)>& line) = 0;
      virtual void OpenXcodeAccounts() = 0;
      virtual void OpenXcode() = 0;
      virtual void OpenDeviceHelp() = 0;
   };

   // The real one. Never construct it in a selftest.
   // `bundledSource` is Infinite.app/Contents/Resources/WatchRemote (the project and install.sh).
   std::unique_ptr<IToolchain> MakeSystemToolchain(const std::string& bundledSource);

   // The team to build with: the free Personal Team if there is one, else the first.
   const Team* PickTeam(const std::vector<Team>& teams);

   // Parsers of the system tools' output (public so the test can feed them captured text).
   std::vector<Team> ParseTeams(const std::string& defaultsOutput);
   std::vector<Device> ParseDevices(const std::string& devicectlJson);

   enum class StepState { kPending, kOk, kMissing };
   enum class StepId { kXcode, kAppleId, kDevices, kInstall };

   struct StepInfo
   {
      StepId id;
      StepState state = StepState::kPending;
      std::string text;      // one sentence: what is done, or what to do
      bool canOpen = false;  // show an Open button
   };

   enum class InstallPhase { kIdle, kRunning, kDone, kFailedOpenedXcode, kFailed };

   struct InstallStatus
   {
      InstallPhase phase = InstallPhase::kIdle;
      std::string progress;   // latest STEP line
      std::string error;      // last error line
   };

   class Checklist
   {
   public:
      // `stateFile` holds the install date; `now` is injectable so a test can move the clock.
      Checklist(std::unique_ptr<IToolchain> tools, std::string stateFile, std::function<time_t()> now = nullptr);
      ~Checklist();

      // Scan on a background thread, at most one at a time. Poll() reads the latest result without blocking.
      void RefreshAsync();
      void RefreshNow();              // blocking, for tests
      bool Scanning() const;

      std::vector<StepInfo> Steps() const;
      bool AllReady() const;          // steps 1 to 3 are green
      Team ChosenTeam() const;
      std::string ChosenWatchId() const;

      void OpenStep(StepId id);
      void StartInstall();            // background thread; ignored while one is running
      void WaitInstall();             // for tests
      InstallStatus Install() const;

      // -1 when the app was never installed from here, else whole days until the 7-day signing runs out (>= 0).
      int DaysLeft() const;
      bool ReinstallDue() const;      // 2 days or fewer left, or already expired
      std::string ExpiryText() const; // "watch app expires in 5 days", "", ...

   private:
      void SaveInstallDate(time_t t);
      void LoadInstallDate();
      struct Impl;
      std::unique_ptr<Impl> m;
   };

   constexpr int kSigningDays = 7;
   constexpr int kReinstallWithinDays = 2;
}
