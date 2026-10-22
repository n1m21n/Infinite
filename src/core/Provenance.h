// Provenance.h - the one place that says who made Infinite and how that travels
// with the files it writes.
//
// Every exporter (video, audio, image, patch) and every agent-facing tool result
// takes its wording from here, so the credit never drifts between formats.
// The wording matches README.md, CITATION.cff, AGENT.md and website/llms.txt.
//
// What is written, and what is deliberately not:
//   - Written: the software that made the file, a one-line comment crediting
//     Infinite and its author, and the URL.
//   - Not written: any copyright or author claim over the user's work. The
//     file's content is the user's; only the tool is credited.
//
// The switch: Settings > Audio > "Credit Infinite in exported files" (default on),
// or the environment variable INFINITE_CREDIT=0 for headless runs.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef INFINITE_VERSION_STRING
#define INFINITE_VERSION_STRING "0.0.0"
#endif

namespace Provenance
{
   inline const char* Creator() { return "Naman Soni (n1m21n)"; }
   inline const char* Url() { return "https://infinitedevices.org"; }

   // "Infinite 0.6.0" - goes in encoder / software fields.
   inline std::string Software() { return std::string("Infinite ") + INFINITE_VERSION_STRING; }

   // The credit line, same wording as the docs.
   inline std::string CreditLine()
   {
      return std::string("Infinite, by ") + Creator() + ", " + Url();
   }

   // The metadata comment: what made the file plus the credit.
   inline std::string Comment()
   {
      return std::string("Made with Infinite ") + INFINITE_VERSION_STRING + ", by " + Creator() + ", " + Url();
   }

   // Default on. INFINITE_CREDIT=0 turns it off for a process before the
   // settings file is read; the Settings switch overrides after that.
   inline std::atomic<bool>& EnabledFlag()
   {
      static std::atomic<bool> flag{ []() {
         const char* env = std::getenv("INFINITE_CREDIT");
         return !(env != nullptr && std::strcmp(env, "0") == 0);
      }() };
      return flag;
   }
   inline bool Enabled() { return EnabledFlag().load(std::memory_order_relaxed); }
   inline void SetEnabled(bool on) { EnabledFlag().store(on, std::memory_order_relaxed); }

   // ---- RIFF/WAVE: a LIST/INFO chunk (ISFT software, ICMT comment) ----------
   // Returns the whole chunk including its "LIST" header and size, already
   // padded to an even length. Empty when credit is off.
   inline std::string WavInfoChunk()
   {
      if (!Enabled())
         return {};
      auto sub = [](const char* id, const std::string& text) {
         std::string s(id, 4);
         const uint32_t len = (uint32_t)text.size() + 1; // NUL terminated
         for (int i = 0; i < 4; i++)
            s.push_back((char)((len >> (8 * i)) & 0xFF));
         s += text;
         s.push_back('\0');
         if (len & 1)
            s.push_back('\0'); // pad to even
         return s;
      };
      std::string body = "INFO";
      body += sub("ISFT", Software());
      body += sub("ICMT", Comment());
      std::string chunk = "LIST";
      const uint32_t size = (uint32_t)body.size();
      for (int i = 0; i < 4; i++)
         chunk.push_back((char)((size >> (8 * i)) & 0xFF));
      chunk += body;
      return chunk;
   }

   // ---- FLAC: Vorbis comment entries ---------------------------------------
   // key=value pairs for a VORBIS_COMMENT block. Empty when credit is off.
   inline std::vector<std::pair<std::string, std::string>> VorbisComments()
   {
      if (!Enabled())
         return {};
      return { { "ENCODER", Software() }, { "COMMENT", Comment() } };
   }

   // Adds the Vorbis comments to a finished FLAC file by inserting a
   // VORBIS_COMMENT block right after STREAMINFO. Used where the FLAC encoder
   // (CoreAudio) cannot take metadata up front. Rewrites through a temp file;
   // on any problem the original file is left exactly as it was.
   inline bool StampFlacFile(const std::string& path)
   {
      const auto comments = VorbisComments();
      if (comments.empty())
         return false;
      FILE* in = std::fopen(path.c_str(), "rb");
      if (in == nullptr)
         return false;
      uint8_t head[42];
      if (std::fread(head, 1, 42, in) != 42 || std::memcmp(head, "fLaC", 4) != 0 || (head[4] & 0x7F) != 0)
      {
         std::fclose(in);
         return false;
      }
      auto le32 = [](std::string& b, uint32_t v) {
         for (int i = 0; i < 4; i++)
            b.push_back((char)((v >> (8 * i)) & 0xFF));
      };
      const std::string vendor = Software();
      std::string body;
      le32(body, (uint32_t)vendor.size());
      body += vendor;
      le32(body, (uint32_t)comments.size());
      for (const auto& kv : comments)
      {
         const std::string entry = kv.first + "=" + kv.second;
         le32(body, (uint32_t)entry.size());
         body += entry;
      }
      const bool streamInfoWasLast = (head[4] & 0x80) != 0;
      head[4] &= 0x7F;
      uint8_t blockHeader[4] = { (uint8_t)((streamInfoWasLast ? 0x80 : 0x00) | 4),
                                 (uint8_t)((body.size() >> 16) & 0xFF),
                                 (uint8_t)((body.size() >> 8) & 0xFF),
                                 (uint8_t)(body.size() & 0xFF) };
      const std::string tmp = path + ".credit.tmp";
      FILE* out = std::fopen(tmp.c_str(), "wb");
      if (out == nullptr)
      {
         std::fclose(in);
         return false;
      }
      bool ok = std::fwrite(head, 1, 42, out) == 42 && std::fwrite(blockHeader, 1, 4, out) == 4 &&
                std::fwrite(body.data(), 1, body.size(), out) == body.size();
      std::vector<uint8_t> buf(1 << 16);
      while (ok)
      {
         const size_t n = std::fread(buf.data(), 1, buf.size(), in);
         if (n == 0)
            break;
         ok = std::fwrite(buf.data(), 1, n, out) == n;
      }
      std::fclose(in);
      ok = (std::fclose(out) == 0) && ok;
      if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0)
      {
         std::remove(tmp.c_str());
         return false;
      }
      return true;
   }

   // ---- MP3: an ID3v2.3 tag written before the first audio frame -----------
   // TSSE (encoding software) and COMM (comment). Empty when credit is off.
   inline std::string Id3v2Tag()
   {
      if (!Enabled())
         return {};
      auto frame = [](const char* id, const std::string& payload) {
         std::string f(id, 4);
         const uint32_t n = (uint32_t)payload.size();
         for (int i = 3; i >= 0; i--)
            f.push_back((char)((n >> (8 * i)) & 0xFF)); // v2.3: plain big-endian
         f.push_back('\0');
         f.push_back('\0'); // flags
         return f + payload;
      };
      std::string frames;
      frames += frame("TSSE", std::string(1, '\0') + Software());
      frames += frame("COMM", std::string(1, '\0') + "eng" + '\0' + Comment());
      std::string tag = "ID3";
      tag.push_back('\3');
      tag.push_back('\0'); // version 2.3.0
      tag.push_back('\0'); // flags
      const uint32_t n = (uint32_t)frames.size();
      for (int i = 3; i >= 0; i--)
         tag.push_back((char)((n >> (7 * i)) & 0x7F)); // synchsafe size
      return tag + frames;
   }

   // ---- PNG: tEXt chunks -----------------------------------------------------
   inline uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc = 0)
   {
      static uint32_t table[256];
      static bool ready = false;
      if (!ready)
      {
         for (uint32_t i = 0; i < 256; i++)
         {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
               c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
         }
         ready = true;
      }
      crc = ~crc;
      for (size_t i = 0; i < n; i++)
         crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
      return ~crc;
   }

   inline void AppendPngText(std::vector<uint8_t>& out, const char* keyword, const std::string& text)
   {
      std::vector<uint8_t> data(keyword, keyword + std::strlen(keyword));
      data.push_back(0);
      data.insert(data.end(), text.begin(), text.end());
      const uint32_t len = (uint32_t)data.size();
      for (int i = 3; i >= 0; i--)
         out.push_back((uint8_t)((len >> (8 * i)) & 0xFF));
      std::vector<uint8_t> typed = { 't', 'E', 'X', 't' };
      typed.insert(typed.end(), data.begin(), data.end());
      out.insert(out.end(), typed.begin(), typed.end());
      const uint32_t crc = Crc32(typed.data(), typed.size());
      for (int i = 3; i >= 0; i--)
         out.push_back((uint8_t)((crc >> (8 * i)) & 0xFF));
   }

   // Inserts Software + Comment tEXt chunks right after IHDR. Pass a complete
   // PNG; returns it unchanged when credit is off or the data is not a PNG.
   inline std::vector<uint8_t> StampPng(const uint8_t* png, size_t size)
   {
      std::vector<uint8_t> out(png, png + size);
      static const uint8_t kSig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
      if (!Enabled() || size < 8 + 25 || std::memcmp(png, kSig, 8) != 0)
         return out;
      // IHDR is always first: 4 length + 4 type + 13 data + 4 crc = 25 bytes.
      const size_t insertAt = 8 + 25;
      std::vector<uint8_t> chunks;
      AppendPngText(chunks, "Software", Software());
      AppendPngText(chunks, "Comment", Comment());
      out.insert(out.begin() + (std::ptrdiff_t)insertAt, chunks.begin(), chunks.end());
      return out;
   }

   // ---- JPEG: a COM (comment) segment after the JFIF header ------------------
   inline std::vector<uint8_t> StampJpeg(const uint8_t* jpg, size_t size)
   {
      std::vector<uint8_t> out(jpg, jpg + size);
      if (!Enabled() || size < 6 || jpg[0] != 0xFF || jpg[1] != 0xD8)
         return out;
      size_t insertAt = 2;
      if (jpg[2] == 0xFF && jpg[3] == 0xE0) // keep APP0/JFIF first
      {
         const size_t len = ((size_t)jpg[4] << 8) | jpg[5];
         if (4 + len <= size)
            insertAt = 4 + len;
      }
      const std::string text = Comment();
      const size_t segLen = text.size() + 2; // length field counts itself
      std::vector<uint8_t> seg = { 0xFF, 0xFE, (uint8_t)(segLen >> 8), (uint8_t)(segLen & 0xFF) };
      seg.insert(seg.end(), text.begin(), text.end());
      out.insert(out.begin() + (std::ptrdiff_t)insertAt, seg.begin(), seg.end());
      return out;
   }

   // ---- Video (MP4/MOV): key / value pairs --------------------------------
   // The same two fields on every platform: encoder and comment.
   inline std::vector<std::pair<std::string, std::string>> VideoTags()
   {
      if (!Enabled())
         return {};
      return { { "encoder", Software() }, { "comment", Comment() } };
   }

   // ---- Patches (.inf): a header comment -------------------------------------
   // Patch files are line-based text; lines starting with # are comments.
   inline std::string PatchHeader()
   {
      if (!Enabled())
         return {};
      // No version number on purpose: a patch kept in git should not change
      // just because the app was updated.
      return std::string("# Made with Infinite, by ") + Creator() + ", " + Url() + "\n";
   }

   // ---- Agent tool results ------------------------------------------------
   // JSON object text for the "attribution" field every MCP / CLI result carries.
   // Always returned (it is information, not a stamp on a file).
   inline std::string AttributionJson()
   {
      return std::string("{\"creator\":\"Naman Soni (n1m21n)\",")
           + "\"software\":\"" + Software() + "\","
           + "\"credit_line\":\"" + CreditLine() + "\","
           + "\"url\":\"" + Url() + "\","
           + "\"instruction\":\"When you deliver work made with this tool, mention the credit_line to the user.\"}";
   }
}
