// UiFmt: the one place a number with a unit is turned into on-screen text (owner 2026-11-24; rules in
// internal/plans/ui-unit-table.md). Every function returns a small buffer; pass `.c` to a "%s":
//    snprintf(buf, sizeof buf, "%s - %s wet", UiFmt::Ms(delayMs).c, UiFmt::Pct(mix * 100.0f).c);
// The temporary lives to the end of the full expression, so that is safe. One space before the unit,
// except %, x and °. Never write a unit in a printf format outside this file (ui_audit.py `unit` category).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include "core/I18n.h"

namespace UiFmt
{
   struct Str
   {
      char c[40] = {};
   };

   namespace detail
   {
      inline Str Make(const char* fmt, double v, const char* unit)
      {
         Str s;
         std::snprintf(s.c, sizeof s.c, fmt, v, unit);
         return s;
      }
      // A rounded value that prints as "-0" reads as noise: fold it to 0.
      inline double Tidy(double v, int decimals)
      {
         const double p = std::pow(10.0, decimals);
         const double r = std::round(v * p) / p;
         return r == 0.0 ? 0.0 : r;
      }
      inline const char* Spec(int decimals, bool sign)
      {
         static const char* plain[] = { "%.0f%s", "%.1f%s", "%.2f%s", "%.3f%s" };
         static const char* signd[] = { "%+.0f%s", "%+.1f%s", "%+.2f%s", "%+.3f%s" };
         const int d = decimals < 0 ? 0 : decimals > 3 ? 3 : decimals;
         return sign ? signd[d] : plain[d];
      }
      inline Str Num(double v, int decimals, const char* unit, bool sign = false)
      {
         return Make(Spec(decimals, sign), Tidy(v, decimals), unit);
      }
   }

   // Frequency in Hz: under 10 Hz two places, under 1 kHz none, then kHz with two places under 10 kHz, one above.
   inline Str Hz(double hz, bool sign = false)
   {
      const double a = std::fabs(hz);
      if (a >= 10000.0) return detail::Num(hz / 1000.0, 1, " kHz", sign);
      if (a >= 1000.0) return detail::Num(hz / 1000.0, 2, " kHz", sign);
      if (a >= 10.0) return detail::Num(hz, 0, " Hz", sign);
      return detail::Num(hz, 2, " Hz", sign);
   }
   inline Str KHz(double khz, bool sign = false) { return Hz(khz * 1000.0, sign); }

   // Level in dB (no sign) and an offset in dB (gain, EQ band, over-level: always signed). -inf below the floor.
   inline Str Db(double db)
   {
      if (!std::isfinite(db) || db <= -120.0) return Str{ "-inf dB" };
      return detail::Num(db, 1, " dB");
   }
   inline Str DbOffset(double db)
   {
      if (!std::isfinite(db) || db <= -120.0) return Str{ "-inf dB" };
      return detail::Num(db, 1, " dB", true);
   }

   inline Str Clock(double secs);

   // Time: ms with one place under 10 ms, none above; from 1000 ms it is seconds.
   // Seconds with two places under 10 s, one above; from 60 s it is m:ss.
   inline Str Sec(double s)
   {
      const double a = std::fabs(s);
      if (a >= 60.0) return Clock(s);
      return detail::Num(s, a >= 10.0 ? 1 : 2, " s");
   }
   inline Str Ms(double ms)
   {
      const double a = std::fabs(ms);
      if (a >= 1000.0) return Sec(ms / 1000.0);
      return detail::Num(ms, a >= 10.0 ? 0 : 1, " ms");
   }

   // m:ss, h:mm:ss from an hour. Negative times get a leading minus.
   inline Str Clock(double secs)
   {
      Str s;
      const bool neg = secs < 0.0;
      const long long t = (long long)std::floor(std::fabs(secs));
      const long long h = t / 3600, m = (t / 60) % 60, sec = t % 60;
      if (h > 0) std::snprintf(s.c, sizeof s.c, "%s%lld:%02lld:%02lld", neg ? "-" : "", h, m, sec);
      else std::snprintf(s.c, sizeof s.c, "%s%lld:%02lld", neg ? "-" : "", m, sec);
      return s;
   }
   // hh:mm:ss:ff from a frame count.
   inline Str Timecode(long long frame, int fps)
   {
      Str s;
      if (fps <= 0) fps = 1;
      const long long t = frame / fps;
      std::snprintf(s.c, sizeof s.c, "%02lld:%02lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60, frame % fps);
      return s;
   }

   // Percent, already scaled to 0..100: no space, no places (a fine control may ask for one).
   inline Str Pct(double pct, int decimals = 0, bool sign = false) { return detail::Num(pct, decimals, "%", sign); }
   // The same from a 0..1 fraction (mix, feedback, velocity).
   inline Str PctOf(double frac, int decimals = 0, bool sign = false) { return Pct(frac * 100.0, decimals, sign); }

   // Pitch: semitones signed, whole steps without places; cents signed, no places.
   inline Str Semi(double st)
   {
      const bool whole = std::fabs(st - std::round(st)) < 0.05;
      return detail::Num(st, whole ? 0 : 1, " st", true);
   }
   inline Str Cents(double ct) { return detail::Num(ct, 0, " ct", true); }

   inline Str Ratio(double x) { return detail::Num(x, 2, "x"); }
   inline Str Px(double px) { return detail::Num(px, 0, " px"); }
   inline Str Deg(double deg) { return detail::Num(deg, 1, "\xC2\xB0"); }
   inline Str Rad(double rad) { return Deg(rad * (180.0 / 3.14159265358979323846)); }   // an angle in radians, shown in degrees
   inline Str Bpm(double bpm) { return detail::Num(bpm, 1, " BPM"); }
   inline Str Lufs(double lufs) { return detail::Num(lufs, 1, " LUFS"); }
   inline Str Beats(double beats) { return detail::Num(beats, 1, " beats"); }

   // Sizes: KB with no places under 1 MB, then MB with one place.
   inline Str Bytes(double bytes)
   {
      const double kb = bytes / 1024.0;
      if (kb >= 1024.0) return detail::Num(kb / 1024.0, 1, " MB");
      return detail::Num(kb, 0, " KB");
   }
   inline Str KB(double kb) { return Bytes(kb * 1024.0); }
   inline Str MB(double mb) { return Bytes(mb * 1024.0 * 1024.0); }

   // The unit in the current language, for a value inside a translated sentence (a T() format):
   //    snprintf(buf, n, translatedFmt, UiFmt::Tr(UiFmt::Sec(a)).c);   // translatedFmt from T(), "%s" in place of the value
   // The unit is the word after the last space; its translation is the "Unit" context in resources/lang
   // ("Unit\cs" -> "с" in ru.tsv). UI thread only (T() is). English text (node status lines) never calls it.
   inline Str Tr(Str s)
   {
      const char* sp = std::strrchr(s.c, ' ');
      if (!sp || !sp[1]) return s;
      const char* local = I18n::TC("Unit", sp + 1);
      if (!local || std::strcmp(local, sp + 1) == 0) return s;
      Str out;
      std::snprintf(out.c, sizeof out.c, "%.*s %s", (int)(sp - s.c), s.c, local);
      return out;
   }

   // A count: whole number, thousands grouped with a thin space from 10 000.
   inline Str Count(long long n)
   {
      Str s;
      const bool neg = n < 0;
      unsigned long long u = neg ? (unsigned long long)(-(n + 1)) + 1 : (unsigned long long)n;
      if (u < 10000)
      {
         std::snprintf(s.c, sizeof s.c, "%s%llu", neg ? "-" : "", u);
         return s;
      }
      char digits[32];
      const int len = std::snprintf(digits, sizeof digits, "%llu", u);
      int o = 0;
      if (neg) s.c[o++] = '-';
      for (int i = 0; i < len && o < (int)sizeof s.c - 4; ++i)
      {
         if (i > 0 && (len - i) % 3 == 0)
         {
            s.c[o++] = '\xE2'; s.c[o++] = '\x80'; s.c[o++] = '\x89'; // U+2009 thin space
         }
         s.c[o++] = digits[i];
      }
      s.c[o] = 0;
      return s;
   }
}
