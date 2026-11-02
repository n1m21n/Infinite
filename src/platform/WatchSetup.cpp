#include "platform/WatchSetup.h"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

namespace WatchSetup
{
   namespace
   {
      std::string Trim(std::string s)
      {
         while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
         size_t i = 0;
         while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
         return s.substr(i);
      }

      // `key = value;` from a NeXT-style plist line, value unquoted. False for other lines.
      bool KeyValue(const std::string& line, std::string& key, std::string& value)
      {
         const size_t eq = line.find(" = ");
         if (eq == std::string::npos || line.back() != ';')
            return false;
         key = Trim(line.substr(0, eq));
         value = Trim(line.substr(eq + 3, line.size() - eq - 4));
         if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
         return true;
      }

      std::string Lower(std::string s)
      {
         for (char& c : s) c = (char)std::tolower((unsigned char)c);
         return s;
      }
   }

   std::vector<Team> ParseTeams(const std::string& text)
   {
      // `defaults read` prints nested dictionaries; a team is the innermost dict that holds a teamID.
      struct Dict { Team team; bool hasId = false; };
      std::vector<Dict> stack;
      std::vector<Team> out;
      std::istringstream in(text);
      std::string raw;
      while (std::getline(in, raw))
      {
         const std::string line = Trim(raw);
         if (line.empty())
            continue;
         if (line.back() == '{' || line.back() == '(')
         {
            stack.emplace_back();
            continue;
         }
         if (line[0] == '}' || line[0] == ')')
         {
            if (!stack.empty())
            {
               if (stack.back().hasId)
                  out.push_back(stack.back().team);
               stack.pop_back();
            }
            continue;
         }
         std::string k, v;
         if (stack.empty() || !KeyValue(line, k, v))
            continue;
         Dict& d = stack.back();
         if (k == "teamID") { d.team.id = v; d.hasId = true; }
         else if (k == "teamName") d.team.name = v;
         else if (k == "isFreeProvisioningTeam") d.team.personal = (v == "1");
      }
      // the same team can appear under more than one account
      std::vector<Team> unique;
      for (const Team& t : out)
         if (std::none_of(unique.begin(), unique.end(), [&](const Team& u) { return u.id == t.id; }))
            unique.push_back(t);
      return unique;
   }

   std::vector<Device> ParseDevices(const std::string& jsonText)
   {
      std::vector<Device> out;
      try
      {
         const nlohmann::json j = nlohmann::json::parse(jsonText);
         const auto& list = j.at("result").at("devices");
         for (const auto& d : list)
         {
            Device dev;
            dev.id = d.value("identifier", "");
            const auto& props = d.contains("deviceProperties") ? d["deviceProperties"] : nlohmann::json::object();
            const auto& hw = d.contains("hardwareProperties") ? d["hardwareProperties"] : nlohmann::json::object();
            dev.name = props.value("name", "");
            dev.developerMode = props.value("developerModeStatus", "") == "enabled";
            const std::string kind = Lower(hw.value("deviceType", "") + " " + hw.value("platform", "") + " " + hw.value("productType", ""));
            dev.isWatch = kind.find("watch") != std::string::npos;
            if (!dev.id.empty())
               out.push_back(dev);
         }
      }
      catch (const std::exception&)
      {
         out.clear();
      }
      return out;
   }

   const Team* PickTeam(const std::vector<Team>& teams)
   {
      for (const Team& t : teams)
         if (t.personal)
            return &t;
      return teams.empty() ? nullptr : &teams.front();
   }

   // ---------------------------------------------------------------------------------------------------------------

   struct Checklist::Impl
   {
      std::unique_ptr<IToolchain> tools;
      std::string stateFile;
      std::function<time_t()> now;

      mutable std::mutex mu;
      Probe probe;
      bool scanned = false, scanning = false;
      InstallStatus install;
      time_t installedAt = 0;
      std::thread scanThread, installThread;
   };

   Checklist::Checklist(std::unique_ptr<IToolchain> tools, std::string stateFile, std::function<time_t()> now)
      : m(new Impl)
   {
      m->tools = std::move(tools);
      m->stateFile = std::move(stateFile);
      m->now = now ? now : [] { return std::time(nullptr); };
      LoadInstallDate();
   }

   Checklist::~Checklist()
   {
      if (m->scanThread.joinable()) m->scanThread.join();
      if (m->installThread.joinable()) m->installThread.join();
   }

   void Checklist::LoadInstallDate()
   {
      std::ifstream in(m->stateFile);
      long long t = 0;
      if (in >> t && t > 0)
         m->installedAt = (time_t)t;
   }

   void Checklist::SaveInstallDate(time_t t)
   {
      {
         std::lock_guard<std::mutex> lk(m->mu);
         m->installedAt = t;
      }
      std::ofstream out(m->stateFile, std::ios::trunc);
      out << (long long)t << "\n";
   }

   void Checklist::RefreshNow()
   {
      Probe p = m->tools->Scan();
      std::lock_guard<std::mutex> lk(m->mu);
      m->probe = std::move(p);
      m->scanned = true;
   }

   void Checklist::RefreshAsync()
   {
      {
         std::lock_guard<std::mutex> lk(m->mu);
         if (m->scanning)
            return;
         m->scanning = true;
      }
      if (m->scanThread.joinable())
         m->scanThread.join();
      m->scanThread = std::thread([this]
      {
         RefreshNow();
         std::lock_guard<std::mutex> lk(m->mu);
         m->scanning = false;
      });
   }

   bool Checklist::Scanning() const
   {
      std::lock_guard<std::mutex> lk(m->mu);
      return m->scanning;
   }

   Team Checklist::ChosenTeam() const
   {
      std::lock_guard<std::mutex> lk(m->mu);
      const Team* t = PickTeam(m->probe.teams);
      return t ? *t : Team();
   }

   std::string Checklist::ChosenWatchId() const
   {
      std::lock_guard<std::mutex> lk(m->mu);
      for (const Device& d : m->probe.devices)
         if (d.isWatch && d.developerMode)
            return d.id;
      return {};
   }

   std::vector<StepInfo> Checklist::Steps() const
   {
      Probe p;
      InstallStatus inst;
      bool scanned;
      time_t installedAt;
      {
         std::lock_guard<std::mutex> lk(m->mu);
         p = m->probe; inst = m->install; scanned = m->scanned; installedAt = m->installedAt;
      }
      std::vector<StepInfo> steps = { { StepId::kXcode }, { StepId::kAppleId }, { StepId::kDevices }, { StepId::kInstall } };
      if (!p.supported)
      {
         for (StepInfo& s : steps) { s.state = StepState::kMissing; s.text = "The watch app needs a Mac with Xcode."; }
         return steps;
      }
      if (!scanned)
      {
         for (StepInfo& s : steps) s.text = "Checking...";
         return steps;
      }

      StepInfo& x = steps[0];
      x.state = p.xcodeInstalled ? StepState::kOk : StepState::kMissing;
      x.text = p.xcodeInstalled ? "Xcode " + p.xcodeVersion + " is installed."
                                : "Install Xcode from the App Store and open it once.";
      x.canOpen = !p.xcodeInstalled;

      StepInfo& a = steps[1];
      const Team* t = PickTeam(p.teams);
      a.state = t ? StepState::kOk : StepState::kMissing;
      a.text = t ? "Signed in as " + (t->name.empty() ? t->id : t->name) + "."
                 : "Sign in with your Apple ID in Xcode: Settings > Accounts > +.";
      a.canOpen = !t && p.xcodeInstalled;

      StepInfo& d = steps[2];
      const Device* watch = nullptr;
      bool anyWatch = false;
      for (const Device& dev : p.devices)
      {
         if (!dev.isWatch) continue;
         anyWatch = true;
         if (dev.developerMode) { watch = &dev; break; }
      }
      if (watch)
      {
         d.state = StepState::kOk;
         d.text = (watch->name.empty() ? std::string("Your watch") : watch->name) + " is ready.";
      }
      else
      {
         d.state = StepState::kMissing;
         d.text = anyWatch ? "Turn on Developer Mode on the watch: Settings > Privacy & Security."
                           : p.devices.empty()
                                ? "Connect your iPhone to this Mac with a cable once, unlock it, and turn on Developer Mode."
                                : "Your watch is not visible: unlock it, and turn on Developer Mode on the iPhone and the watch.";
         d.canOpen = true;
      }

      StepInfo& i = steps[3];
      const bool ready = x.state == StepState::kOk && a.state == StepState::kOk && d.state == StepState::kOk;
      if (!p.projectBundled)
      {
         i.state = StepState::kMissing;
         i.text = "This copy of Infinite was built without the watch app project.";
      }
      else if (inst.phase == InstallPhase::kRunning)
      {
         i.text = inst.progress.empty() ? "Working..." : inst.progress;
      }
      else if (inst.phase == InstallPhase::kFailedOpenedXcode)
      {
         i.state = StepState::kMissing;
         i.text = "Xcode is open with your team filled in: pick your watch and press Run. (" + inst.error + ")";
      }
      else if (inst.phase == InstallPhase::kFailed)
      {
         i.state = StepState::kMissing;
         i.text = "Install failed: " + inst.error;
      }
      else if (inst.phase == InstallPhase::kDone || installedAt > 0)
      {
         i.state = StepState::kOk;
         i.text = "Installed. Open Infinite Remote on the watch.";
      }
      else
      {
         i.text = ready ? "Ready to install." : "Finish the steps above first.";
      }
      return steps;
   }

   bool Checklist::AllReady() const
   {
      const std::vector<StepInfo> s = Steps();
      return s[0].state == StepState::kOk && s[1].state == StepState::kOk && s[2].state == StepState::kOk;
   }

   void Checklist::OpenStep(StepId id)
   {
      if (id == StepId::kXcode || id == StepId::kAppleId)
         id == StepId::kXcode ? m->tools->OpenXcode() : m->tools->OpenXcodeAccounts();
      else if (id == StepId::kDevices)
         m->tools->OpenDeviceHelp();
   }

   void Checklist::StartInstall()
   {
      if (!AllReady())
         return;
      InstallRequest req;
      req.teamId = ChosenTeam().id;
      req.deviceId = ChosenWatchId();
      {
         std::lock_guard<std::mutex> lk(m->mu);
         if (m->install.phase == InstallPhase::kRunning)
            return;
         m->install = InstallStatus();
         m->install.phase = InstallPhase::kRunning;
      }
      if (m->installThread.joinable())
         m->installThread.join();
      m->installThread = std::thread([this, req]
      {
         bool fellBack = false;
         const bool ok = m->tools->Install(req, [&](const std::string& line)
         {
            std::lock_guard<std::mutex> lk(m->mu);
            if (line.rfind("STEP ", 0) == 0) m->install.progress = line.substr(5);
            else if (line.rfind("ERROR ", 0) == 0) m->install.error = line.substr(6);
            else if (line == "FALLBACK") fellBack = true;
         });
         if (ok)
            SaveInstallDate(m->now());
         std::lock_guard<std::mutex> lk(m->mu);
         m->install.phase = ok ? InstallPhase::kDone : fellBack ? InstallPhase::kFailedOpenedXcode : InstallPhase::kFailed;
      });
   }

   void Checklist::WaitInstall()
   {
      if (m->installThread.joinable())
         m->installThread.join();
   }

   InstallStatus Checklist::Install() const
   {
      std::lock_guard<std::mutex> lk(m->mu);
      return m->install;
   }

   int Checklist::DaysLeft() const
   {
      time_t at;
      {
         std::lock_guard<std::mutex> lk(m->mu);
         at = m->installedAt;
      }
      if (at <= 0)
         return -1;
      const long long left = (long long)at + (long long)kSigningDays * 86400 - (long long)m->now();
      return left <= 0 ? 0 : (int)((left + 86399) / 86400);
   }

   bool Checklist::ReinstallDue() const
   {
      const int d = DaysLeft();
      return d >= 0 && d <= kReinstallWithinDays;
   }

   std::string Checklist::ExpiryText() const
   {
      const int d = DaysLeft();
      if (d < 0)
         return {};
      if (d == 0)
         return "watch app has expired";
      return "watch app expires in " + std::to_string(d) + (d == 1 ? " day" : " days");
   }
}
