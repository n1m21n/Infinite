// SPDX-License-Identifier: GPL-3.0-only
// Infinite-Turbo full build only (INFINITE_ENABLE_GPL). See src/gpl/README.md.
//
// Synth REF controller profiles (AUTO MAP). A profile is a JSON file for one
// combination CONTROLLER + MIDI MODE + INSTRUMENT FIRMWARE: which messages of
// the controller drive which physical control of the instrument, and how the
// controller reports motion (absolute, or one of the relative encodings).
// The instrument's own control kinds (relative encoder, absolute fader...)
// come from the core; SrMapTable bridges the two. Bundled profiles live in
// <exe>/synthref/profiles, the user's own in Documents\Infinite-Turbo\SynthRef\
// profiles (same id = the user's wins). Schema: docs/SYNTH-REF.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum SrTarget : uint8_t
{
   kSrNone = 0,
   kSrEncoder,  // SR_CTL_ENCODER id
   kSrFader,    // SR_CTL_FADER id
   kSrButton,   // SR_CTL_BUTTON id, held while the CC is >= 64 / the note is down
   kSrPad,      // SR_CTL_PAD id, velocity
   kSrPedal,    // SR_CTL_PEDAL id
   kSrPressure, // SR_CTL_PRESSURE id
   kSrKeyboard, // play the note on the instrument's keyboard track
   kSrTargets
};

// How the controller sends a knob.
enum SrInput : uint8_t
{
   kSrAbsolute = 0, // 0..127 position (turned into detents for a relative control)
   kSrRel64,        // 64 = still, 65.. = +1.., 63.. = -1.. (Arturia "Relative #1")
   kSrRel2c,        // 1.. = +1.., 127.. = -1.. (two's complement, "Relative #2")
   kSrRel16,        // 16 = still, 17.. = +, 15.. = - ("Relative #3")
   kSrInputs
};

struct SrMapEntry
{
   uint8_t target = kSrNone;
   uint8_t id = 0;
   uint8_t input = kSrAbsolute;
   uint8_t spare = 0;
};

// Plain data, read by the audio thread: no pointers, no strings.
struct SrMapTable
{
   SrMapEntry cc[16][128];
   SrMapEntry note[16][128];
   int keyboardChannel = -1; // -1 = notes on any unmapped channel play the keyboard
   bool passBend = true;     // pitch bend -> keyboard track
   bool passMod = true;      // CC1 -> keyboard track
   bool passPressure = true; // channel pressure -> keyboard track
   bool sustainToPedal = true;
   bool valid = false;
};

struct SrProfile
{
   std::string id, name, core, mode, file;
   std::vector<std::string> match;   // a device key containing one of these (case-insensitive)
   std::vector<std::string> exclude; // ... and none of these
   std::vector<std::string> lines;   // human-readable assignments for the panel
   SrMapTable table;
};

// Loads every profile (bundled, then user). Errors go to `errors`.
std::vector<SrProfile> SrLoadProfiles(std::vector<std::string>& errors);
// The profile for this device and core: `wantedId` if given and loaded,
// otherwise the first whose match/exclude accepts the device key.
const SrProfile* SrFindProfile(const std::vector<SrProfile>& profiles, const std::string& deviceKey,
                               const std::string& coreId, const std::string& wantedId);
bool SrDeviceMatches(const SrProfile& p, const std::string& deviceKey);
const char* SrTargetName(int target);
const char* SrInputName(int input);
// Signed detents from one CC value in a relative encoding (0 for absolute).
int SrRelativeDelta(int input, int value);
