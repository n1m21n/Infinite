#pragma once
// fork: single place for the names that must differ from the official Infinite so that
// "Infinite TU" installs and runs side by side with it (separate data dir, mutex,
// ports, credential service, AppUserModelID). See docs/fork-changes.md.
namespace ForkIdentity
{
   constexpr const char*    kDisplayName   = "Infinite TU";
   constexpr const char*    kDataDirName   = "InfiniteTU";        // %APPDATA%\InfiniteTU, ~/Library/Application Support/InfiniteTU, ~/.config/InfiniteTU
   constexpr const char*    kCredService   = "InfiniteTU";        // Windows Credential Manager / Keychain service
   constexpr const wchar_t* kMutexName     = L"Local\\InfiniteTU.SingleInstance";
   constexpr const wchar_t* kAppUserModel  = L"InfiniteTU.Synthesizer";
   constexpr int            kControlPort   = 7877;                // official: 7777
   constexpr int            kOscPort       = 9200;                // official: 9100
   constexpr int            kMcpPort       = 7890;                // official: 7790
}
