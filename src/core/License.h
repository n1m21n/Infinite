#pragma once
// Plugin licence (docs/plans/vst-au/README.md 8.5, step 2.6). A key is `INF1.<b64url payload>.<b64url sig>`:
// Ed25519 (Monocypher) over the bytes "INF1.<b64url payload>", payload JSON {v, id, email, txn, seats,
// major, iat}, signed by worker/license (index.js signLicense). Checked offline against the public key
// compiled in from worker/license/PUBLIC_KEY.txt.
//
// The plugin shell checks it before it spawns an engine (no key: licence sheet only, Instrument
// silent, FX dry). The engine reads only the key's id, for the weekly refund list. The app never
// calls any of this. No dependency beyond Monocypher and nlohmann json, so the shell builds it too.
//
// Files, one set per user, shared by both builds and every instance (Dir()):
//   license.inflic          the key text
//   license-revoked.json    {"revoked":[ids], "fetched":<unix s>}, the last refund list downloaded
//   license-settings.json   {"refundCheck":true|false}
#include <cstdint>
#include <string>
#include <vector>

namespace License
{
   enum class Status : int
   {
      None = 0,         // no key stored
      Valid = 1,
      Malformed = 2,    // not an INF1 key
      BadSignature = 3, // not signed by Infinite (or edited)
      WrongVersion = 4, // payload v != 1
      WrongMajor = 5,   // unused: keys carry lifetime updates (kept so the numbers stay stable)
      Revoked = 6,      // refunded
   };

   struct Info
   {
      Status status = Status::None;
      std::string id;    // INF-XXXX-XXXX
      std::string email;
      int major = 0;
   };

   // Signature, v == 1, id not in revoked. Any major works: lifetime updates.
   Info Check(const std::string& key, const std::vector<std::string>& revoked, int appMajor);

   // The first INF1 key in pasted text or a dropped file's contents (whitespace, quotes and line
   // breaks around or inside it are dropped). Empty when there is none.
   std::string Extract(const std::string& text);

   // The key's id without checking the signature (the engine's refund check).
   std::string IdOf(const std::string& key);

   // The stored key, checked against the cached refund list.
   Info Current(int appMajor);

   std::string Dir(); // $INFINITE_LICENSE_DIR, else the per-user config folder (README 8.5)
   bool ReadKey(std::string& key);
   bool WriteKey(const std::string& key);

   std::vector<std::string> ReadRevoked(int64_t* fetchedUnix = nullptr);
   // `json` is the body of GET /api/license/revoked; false (cache kept) when it does not parse.
   bool WriteRevoked(const std::string& json, int64_t fetchedUnix);

   bool RefundCheckOn();
   void SetRefundCheck(bool on);

   // Sheet text for a status that is not Valid ("This key was refunded", "Your key covers 1.x").
   std::string Message(const Info& info);

   constexpr const char* kRevokedUrl = "https://infinitedevices.org/api/license/revoked";
   // kRevokedUrl, or $INFINITE_LICENSE_REVOKED_URL (checks serve their own list).
   std::string RevokedUrl();
   constexpr const char* kBuyUrl = "https://infinitedevices.org/buy.html";
   constexpr int64_t kRefundCheckEverySeconds = 7 * 24 * 3600;
}
