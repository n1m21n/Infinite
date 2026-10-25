// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
#include "SynthRefMidi.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include <juce_core/juce_core.h>

#include "SynthRefCore.h"

namespace fs = std::filesystem;

namespace
{
   std::string Lower(std::string s)
   {
      std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
   }

   int TargetFromName(const juce::String& s)
   {
      static const char* names[kSrTargets] = { "none", "encoder", "fader", "button", "pad", "pedal", "pressure", "keyboard" };
      for (int i = 0; i < kSrTargets; ++i)
         if (s.equalsIgnoreCase(names[i]))
            return i;
      return -1;
   }

   int InputFromName(const juce::String& s)
   {
      static const char* names[kSrInputs] = { "absolute", "relative64", "relative2c", "relative16" };
      for (int i = 0; i < kSrInputs; ++i)
         if (s.equalsIgnoreCase(names[i]))
            return i;
      return -1;
   }

   // "channel": 1..16, or "any" / missing = every channel.
   void Channels(const juce::var& v, int& first, int& last)
   {
      first = 0;
      last = 15;
      if (v.isInt() || v.isDouble())
      {
         const int c = std::clamp((int)v - 1, 0, 15);
         first = last = c;
      }
   }

   void Strings(const juce::var& v, std::vector<std::string>& out)
   {
      if (const auto* a = v.getArray())
         for (const auto& s : *a)
            out.push_back(s.toString().toStdString());
   }

   bool Parse(const fs::path& file, SrProfile& p, std::string& error)
   {
      const juce::File f(juce::String(juce::CharPointer_UTF8(file.u8string().c_str())));
      juce::var root;
      const juce::Result r = juce::JSON::parse(f.loadFileAsString(), root);
      if (r.failed() || !root.isObject())
      {
         error = file.filename().u8string() + ": " + (r.failed() ? r.getErrorMessage().toStdString() : "not a JSON object");
         return false;
      }
      if ((int)root["schema"] != 1)
      {
         error = file.filename().u8string() + ": schema must be 1";
         return false;
      }
      p.file = file.u8string();
      p.id = root["id"].toString().toStdString();
      p.name = root["name"].toString().toStdString();
      p.core = root["core"].toString().toStdString();
      p.mode = root["mode"].toString().toStdString();
      if (p.id.empty() || p.core.empty())
      {
         error = file.filename().u8string() + ": needs \"id\" and \"core\"";
         return false;
      }
      Strings(root["device"]["match"], p.match);
      Strings(root["device"]["exclude"], p.exclude);
      SrMapTable& t = p.table;

      const juce::var keyboard = root["keyboard"];
      if (keyboard.isObject() && (keyboard["channel"].isInt() || keyboard["channel"].isDouble()))
         t.keyboardChannel = std::clamp((int)keyboard["channel"] - 1, 0, 15);

      const juce::var pads = root["pads"];
      if (pads.isObject())
      {
         int c0, c1;
         Channels(pads["channel"], c0, c1);
         const int firstNote = pads.hasProperty("first_note") ? (int)pads["first_note"] : 36;
         const int count = std::clamp(pads.hasProperty("count") ? (int)pads["count"] : 16, 0, 128);
         const int firstId = pads.hasProperty("first_id") ? (int)pads["first_id"] : 0;
         for (int c = c0; c <= c1; ++c)
            for (int i = 0; i < count; ++i)
               if (firstNote + i >= 0 && firstNote + i < 128)
                  t.note[c][firstNote + i] = { kSrPad, (uint8_t)(firstId + i), kSrAbsolute, 0 };
         char line[96];
         snprintf(line, sizeof(line), "pads: notes %d-%d ch %s -> pads %d-%d", firstNote, firstNote + count - 1,
                  c0 == c1 ? std::to_string(c0 + 1).c_str() : "any", firstId + 1, firstId + count);
         p.lines.push_back(line);
      }

      if (const auto* controls = root["controls"].getArray())
         for (const auto& c : *controls)
         {
            const bool isNote = c.hasProperty("note");
            const int number = isNote ? (int)c["note"] : (int)c["cc"];
            const int target = TargetFromName(c["target"].toString());
            const int input = c.hasProperty("input") ? InputFromName(c["input"].toString()) : kSrAbsolute;
            if (number < 0 || number > 127 || target < 0 || input < 0)
            {
               error = file.filename().u8string() + ": bad control entry " + juce::JSON::toString(c, true).toStdString();
               return false;
            }
            int c0, c1;
            Channels(c["channel"], c0, c1);
            const SrMapEntry e { (uint8_t)target, (uint8_t)std::clamp((int)c["id"], 0, 255), (uint8_t)input, 0 };
            for (int ch = c0; ch <= c1; ++ch)
               (isNote ? t.note[ch][number] : t.cc[ch][number]) = e;
            std::string label = c["label"].toString().toStdString();
            char line[160];
            snprintf(line, sizeof(line), "%s %d -> %s %d (%s)%s%s", isNote ? "note" : "CC", number, SrTargetName(target),
                     (int)e.id + 1, SrInputName(input), label.empty() ? "" : ": ", label.c_str());
            p.lines.push_back(line);
         }

      const juce::var pass = root["pass"];
      if (pass.isObject())
      {
         if (pass.hasProperty("pitchbend")) t.passBend = (bool)pass["pitchbend"];
         if (pass.hasProperty("modwheel")) t.passMod = (bool)pass["modwheel"];
         if (pass.hasProperty("pressure")) t.passPressure = (bool)pass["pressure"];
         if (pass.hasProperty("sustain")) t.sustainToPedal = pass["sustain"].toString().equalsIgnoreCase("pedal");
      }
      t.valid = true;
      return true;
   }

   void LoadDir(const fs::path& dir, std::map<std::string, SrProfile>& byId, std::vector<std::string>& errors)
   {
      std::error_code ec;
      if (!fs::is_directory(dir, ec))
         return;
      std::vector<fs::path> files;
      for (const auto& e : fs::directory_iterator(dir, ec))
         if (e.path().extension() == ".json")
            files.push_back(e.path());
      std::sort(files.begin(), files.end());
      for (const fs::path& f : files)
      {
         SrProfile p;
         std::string error;
         if (Parse(f, p, error))
            byId[p.id] = std::move(p);
         else
            errors.push_back(error);
      }
   }
}

const char* SrTargetName(int target)
{
   static const char* names[kSrTargets] = { "none", "encoder", "fader", "button", "pad", "pedal", "pressure", "keyboard" };
   return target >= 0 && target < kSrTargets ? names[target] : "?";
}

const char* SrInputName(int input)
{
   static const char* names[kSrInputs] = { "absolute", "relative64", "relative2c", "relative16" };
   return input >= 0 && input < kSrInputs ? names[input] : "?";
}

int SrRelativeDelta(int input, int v)
{
   switch (input)
   {
   case kSrRel64: return v - 64;
   case kSrRel2c: return v < 64 ? v : v - 128;
   case kSrRel16: return v - 16;
   default: return 0;
   }
}

std::vector<SrProfile> SrLoadProfiles(std::vector<std::string>& errors)
{
   std::map<std::string, SrProfile> byId;
   LoadDir(fs::u8path(SynthRefAppDir()) / "profiles", byId, errors);
   const std::string user = SynthRefUserDir();
   if (!user.empty())
   {
      std::error_code ec;
      fs::create_directories(fs::u8path(user) / "profiles", ec);
      LoadDir(fs::u8path(user) / "profiles", byId, errors);
   }
   std::vector<SrProfile> out;
   for (auto& kv : byId)
      out.push_back(std::move(kv.second));
   return out;
}

bool SrDeviceMatches(const SrProfile& p, const std::string& deviceKey)
{
   const std::string key = Lower(deviceKey);
   for (const std::string& x : p.exclude)
      if (!x.empty() && key.find(Lower(x)) != std::string::npos)
         return false;
   for (const std::string& m : p.match)
      if (!m.empty() && key.find(Lower(m)) != std::string::npos)
         return true;
   return false;
}

const SrProfile* SrFindProfile(const std::vector<SrProfile>& profiles, const std::string& deviceKey,
                               const std::string& coreId, const std::string& wantedId)
{
   if (!wantedId.empty())
      for (const SrProfile& p : profiles)
         if (p.id == wantedId && p.core == coreId)
            return &p;
   if (deviceKey.empty())
      return nullptr;
   for (const SrProfile& p : profiles)
      if (p.core == coreId && SrDeviceMatches(p, deviceKey))
         return &p;
   return nullptr;
}
