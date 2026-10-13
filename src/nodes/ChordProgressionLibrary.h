#pragma once

#include <initializer_list>

// Turbo 0.55: the Chord Progression's suggestion library. "Famous" holds
// well-known progressions (common practice, no single author); every other
// entry comes from NT ChordGen 5.1 (Ricardo Palmieri / Noisetupi), its style
// templates: degree = semitones above the key, quality = chord type.
// Plain data, MIT like the rest of Infinite.

namespace ChordLibrary
{
   struct Chord { int degree; const char* quality; };
   struct Progression
   {
      const char* category;
      const char* style;
      const char* name;
      int count;
      Chord chords[8];
   };

   inline const Progression* All(int& count)
   {
      static const Progression kAll[] = {
      { "Famous", "Famous", "Pop axis I-V-vi-IV", 4, { {0, "M"}, {7, "M"}, {9, "m"}, {5, "M"} } },
      { "Famous", "Famous", "Sensitive vi-IV-I-V", 4, { {9, "m"}, {5, "M"}, {0, "M"}, {7, "M"} } },
      { "Famous", "Famous", "Doo-wop I-vi-IV-V", 4, { {0, "M"}, {9, "m"}, {5, "M"}, {7, "M"} } },
      { "Famous", "Famous", "Jazz ii-V-I", 4, { {2, "m7"}, {7, "7"}, {0, "maj7"}, {0, "maj7"} } },
      { "Famous", "Famous", "Turnaround I-vi-ii-V", 4, { {0, "maj7"}, {9, "m7"}, {2, "m7"}, {7, "7"} } },
      { "Famous", "Famous", "Andalusian i-bVII-bVI-V", 4, { {0, "m"}, {10, "M"}, {8, "M"}, {7, "M"} } },
      { "Famous", "Famous", "Pachelbel canon", 8, { {0, "M"}, {7, "M"}, {9, "m"}, {4, "m"}, {5, "M"}, {0, "M"}, {5, "M"}, {7, "M"} } },
      { "Famous", "Famous", "Royal road IV-V-iii-vi", 4, { {5, "maj7"}, {7, "7"}, {4, "m7"}, {9, "m7"} } },
      { "Famous", "Famous", "Blues I-IV-I-V", 4, { {0, "7"}, {5, "7"}, {0, "7"}, {7, "7"} } },
      { "Famous", "Famous", "Minor i-iv-v-i", 4, { {0, "m"}, {5, "m"}, {7, "m"}, {0, "m"} } },
      { "Famous", "Famous", "Epic i-bVI-bIII-bVII", 4, { {0, "m"}, {8, "M"}, {3, "M"}, {10, "M"} } },
      { "Famous", "Famous", "Mixolydian I-bVII-IV-I", 4, { {0, "M"}, {10, "M"}, {5, "M"}, {0, "M"} } },
      { "Famous", "Famous", "Lo-fi ii9-V13-Imaj9-vi9", 4, { {2, "m9"}, {7, "13"}, {0, "maj9"}, {9, "m9"} } },
      { "Famous", "Famous", "Neo soul IVmaj7-iii7-vi7-V", 4, { {5, "maj7"}, {4, "m7"}, {9, "m7"}, {7, "9"} } },
      { "Famous", "Famous", "Dorian i-IV", 4, { {0, "m7"}, {5, "7"}, {0, "m7"}, {5, "7"} } },
      { "Rock / Alternative", "Punk Rock", "Punk Rock 1: I-IV-V-I", 4, { {0, "M"}, {5, "M"}, {7, "M"}, {0, "M"} } },
      { "Rock / Alternative", "Punk Rock", "Punk Rock 2: I-bVII-IV-IV", 4, { {0, "M"}, {10, "M"}, {5, "M"}, {5, "M"} } },
      { "Rock / Alternative", "Punk Rock", "Punk Rock 3: i-bVII-bVI-V", 4, { {0, "m"}, {10, "M"}, {8, "M"}, {7, "M"} } },
      { "Rock / Alternative", "Post-punk", "Post-punk 1: i-bVII-IV-i", 4, { {0, "m"}, {10, "M"}, {5, "M"}, {0, "m"} } },
      { "Rock / Alternative", "Post-punk", "Post-punk 2: i-IV-i-IV", 4, { {0, "m"}, {5, "M"}, {0, "m"}, {5, "M"} } },
      { "Rock / Alternative", "Dream Pop / Shoegaze", "Dream Pop / Shoegaze 1: I-iii-IV-I", 4, { {0, "maj7"}, {4, "m7"}, {5, "maj7"}, {0, "add9"} } },
      { "Rock / Alternative", "Dream Pop / Shoegaze", "Dream Pop / Shoegaze 2: I-bVI-IV-I", 4, { {0, "maj7"}, {8, "maj7"}, {5, "sus2"}, {0, "maj7"} } },
      { "Rock / Alternative", "Indie / Art Rock", "Indie / Art Rock 1: i-bVI-IV-bIII-v", 5, { {0, "m7"}, {8, "maj7"}, {5, "M"}, {3, "maj7"}, {7, "sus2"} } },
      { "Rock / Alternative", "Indie / Art Rock", "Indie / Art Rock 2: I-bVI-bIII-iv", 4, { {0, "add9"}, {8, "M"}, {3, "maj7"}, {5, "m"} } },
      { "Brazil", "Samba", "Samba 1: I-VI7-ii-V", 4, { {0, "6"}, {9, "7"}, {2, "m7"}, {7, "7"} } },
      { "Brazil", "Samba", "Samba 2: I-#Idim-ii-V", 4, { {0, "6"}, {1, "dim7"}, {2, "m7"}, {7, "9"} } },
      { "Brazil", "Bossa Nova", "Bossa Nova 1: ii-V-I-VI7", 4, { {2, "m7"}, {7, "7"}, {0, "maj7"}, {9, "7"} } },
      { "Brazil", "Bossa Nova", "Bossa Nova 2: iii-VI7-ii-V", 4, { {4, "m7"}, {9, "7"}, {2, "m7"}, {7, "7"} } },
      { "Brazil", "Bossa Nova", "Bossa Nova 3: I-Idim-ii-V", 4, { {0, "maj7"}, {0, "dim7"}, {2, "m7"}, {7, "7"} } },
      { "Brazil", "Bossa Nova", "Bossa Nova 4: iiø-V-i-VI7", 4, { {2, "m7b5"}, {7, "7b9"}, {0, "m6"}, {9, "7"} } },
      { "Brazil", "Samba-jazz", "Samba-jazz 1: ii-V-I-VI7", 4, { {2, "m7"}, {7, "13"}, {0, "maj9"}, {9, "7"} } },
      { "Brazil", "Samba-jazz", "Samba-jazz 2: iii-VI-ii-V", 4, { {4, "m7"}, {9, "7b9"}, {2, "m9"}, {7, "13"} } },
      { "Brazil", "Choro", "Choro 1: I-III7-vi-II7-V", 5, { {0, "6"}, {4, "7"}, {9, "m"}, {2, "7"}, {7, "7"} } },
      { "Brazil", "Choro", "Choro 2: I-#Idim-ii-V", 4, { {0, "6"}, {1, "dim7"}, {2, "m7"}, {7, "7"} } },
      { "Brazil", "Baião / Nordeste Modal", "Baião / Nordeste Modal 1: I-bVII-I-IV", 4, { {0, "M"}, {10, "M"}, {0, "6"}, {5, "M"} } },
      { "Brazil", "Baião / Nordeste Modal", "Baião / Nordeste Modal 2: i-IV", 2, { {0, "m"}, {5, "M"} } },
      { "Soul / Jazz / Funk", "Soul / R&B", "Soul / R&B 1: IV-iii-ii-V", 4, { {5, "maj7"}, {4, "m7"}, {2, "m7"}, {7, "9"} } },
      { "Soul / Jazz / Funk", "Soul / R&B", "Soul / R&B 2: I-VI-ii-V", 4, { {0, "maj7"}, {9, "7"}, {2, "m9"}, {7, "13"} } },
      { "Soul / Jazz / Funk", "Gospel", "Gospel 1: I-iii-IV-V", 4, { {0, "M"}, {4, "m7"}, {5, "maj7"}, {7, "sus4"} } },
      { "Soul / Jazz / Funk", "Gospel", "Gospel 2: ii-V-I-I7-IV", 5, { {2, "m7"}, {7, "9"}, {0, "maj7"}, {0, "7"}, {5, "6"} } },
      { "Soul / Jazz / Funk", "Funk", "Funk 1: I9-I9-I9-I9", 4, { {0, "9"}, {0, "9"}, {0, "9"}, {0, "9"} } },
      { "Soul / Jazz / Funk", "Funk", "Funk 2: i-IV-i-IV", 4, { {0, "m7"}, {5, "9"}, {0, "m7"}, {5, "9"} } },
      { "Soul / Jazz / Funk", "Jazz Standard", "Jazz Standard 1: ii-V-I-VI7", 4, { {2, "m7"}, {7, "7"}, {0, "maj7"}, {9, "7"} } },
      { "Soul / Jazz / Funk", "Jazz Standard", "Jazz Standard 2: iiø-V-i-i", 4, { {2, "m7b5"}, {7, "7b9"}, {0, "m6"}, {0, "m6"} } },
      { "Soul / Jazz / Funk", "Jazz Standard", "Jazz Standard 3: I-VI-ii-V", 4, { {0, "maj7"}, {9, "7"}, {2, "m7"}, {7, "7"} } },
      { "Soul / Jazz / Funk", "Modal Jazz", "Modal Jazz 1: i-i-ii-i", 4, { {0, "m11"}, {0, "m11"}, {2, "m11"}, {0, "m11"} } },
      { "Soul / Jazz / Funk", "Modal Jazz", "Modal Jazz 2: SET-SET-SET-SET", 4, { {0, "quartal"}, {0, "quartal"}, {5, "quartal"}, {5, "quartal"} } },
      { "Iberia / Mediterranean", "Flamenco", "Flamenco 1: iv-bIII-bII-I", 4, { {5, "m"}, {3, "M"}, {1, "M"}, {0, "M"} } },
      { "Iberia / Mediterranean", "Flamenco", "Flamenco 2: bII-I-iv-I", 4, { {1, "M"}, {0, "M"}, {5, "m"}, {0, "7b9"} } },
      { "Iberia / Mediterranean", "Flamenco", "Flamenco 3: iv-bII-bIII-bII-I", 5, { {5, "m"}, {1, "M"}, {3, "M"}, {1, "M"}, {0, "M"} } },
      { "Africa / Diaspora", "West African Highlife", "West African Highlife 1: I-IV-V-I", 4, { {0, "M"}, {5, "M"}, {7, "M"}, {0, "M"} } },
      { "Africa / Diaspora", "West African Highlife", "West African Highlife 2: I-vi-IV-V", 4, { {0, "6"}, {9, "m"}, {5, "M"}, {7, "M"} } },
      { "Africa / Diaspora", "Afrobeat", "Afrobeat 1: i-i-i-i", 4, { {0, "m7"}, {0, "m7"}, {0, "m7"}, {0, "m7"} } },
      { "Africa / Diaspora", "Afrobeat", "Afrobeat 2: I-IV-I-IV", 4, { {0, "9"}, {5, "9"}, {0, "9"}, {5, "9"} } },
      { "Africa / Diaspora", "Ethiopian Pentatonic", "Ethiopian Pentatonic 1: SET", 1, { {0, "5"} } },
      { "Modal", "Japanese Pentatonic", "Japanese Pentatonic 1: SET", 1, { {0, "5"} } },
      { "Modal", "Indian Modal", "Indian Modal 1: SET", 1, { {0, "5"} } },
      { "Modal", "Middle Eastern Modal", "Middle Eastern Modal 1: SET", 1, { {0, "5"} } },
      { "Composers / Artists", "Thom Yorke / Radiohead", "Thom Yorke / Radiohead 1: i-III-IV-bVI-bIII", 5, { {0, "m7"}, {4, "M"}, {5, "add9"}, {8, "maj7"}, {3, "maj7"} } },
      { "Composers / Artists", "Thom Yorke / Radiohead", "Thom Yorke / Radiohead 2: i-bII-bVI-IV-bIII-bVII-i", 7, { {0, "sus2"}, {1, "maj7"}, {8, "maj7"}, {5, "M"}, {3, "add9"}, {10, "M"}, {0, "m"} } },
      { "Composers / Artists", "Björk", "Björk 1: I-III-II-bVI-I", 5, { {0, "maj7"}, {4, "maj7"}, {2, "sus2"}, {8, "maj7"}, {0, "5"} } },
      { "Composers / Artists", "Björk", "Björk 2: I-bIII-bVI-IV", 4, { {0, "add9"}, {3, "maj7"}, {8, "sus2"}, {5, "maj7"} } },
      { "Composers / Artists", "Arrigo Barnabé / Serial", "Arrigo Barnabé / Serial 1: SET", 1, { {0, "cluster"} } },
      { "Composers / Artists", "Itamar Assumpção", "Itamar Assumpção 1: i-IV-i-IV", 4, { {0, "m7"}, {5, "9"}, {0, "m7"}, {5, "9"} } },
      { "Composers / Artists", "Itamar Assumpção", "Itamar Assumpção 2: I-I-bVII-I", 4, { {0, "9"}, {0, "9"}, {10, "7"}, {0, "9"} } },
      { "Composers / Artists", "Ryuichi Sakamoto", "Ryuichi Sakamoto 1: I-iii-IV-I", 4, { {0, "maj7"}, {4, "m7"}, {5, "6/9"}, {0, "sus2"} } },
      { "Composers / Artists", "Ryuichi Sakamoto", "Ryuichi Sakamoto 2: I-bVI-bIII-I", 4, { {0, "maj9"}, {8, "maj7"}, {3, "6"}, {0, "maj7"} } },
      { "Composers / Artists", "Clube da Esquina / Milton", "Clube da Esquina / Milton 1: I-bIII-IV-bVI", 4, { {0, "maj7"}, {3, "maj7"}, {5, "add9"}, {8, "maj7"} } },
      { "Composers / Artists", "Clube da Esquina / Milton", "Clube da Esquina / Milton 2: I-iii-iv-bVII", 4, { {0, "6/9"}, {4, "m7"}, {5, "m7"}, {10, "maj7"} } },
      { "Composers / Artists", "Hermeto Pascoal", "Hermeto Pascoal 1: SET-SET-SET-SET-SET", 5, { {0, "quartal"}, {1, "dim7"}, {4, "aug"}, {6, "7"}, {11, "cluster"} } },
      { "Composers / Artists", "Hermeto Pascoal", "Hermeto Pascoal 2: SET-SET-SET-SET", 4, { {0, "aug"}, {2, "quartal"}, {7, "7#9"}, {1, "dim7"} } },
      { "Composers / Artists", "Stevie Wonder", "Stevie Wonder 1: I-III7-vi-I7-IV-V", 6, { {0, "maj7"}, {4, "7"}, {9, "m9"}, {0, "7"}, {5, "maj7"}, {7, "13"} } },
      { "Composers / Artists", "Stevie Wonder", "Stevie Wonder 2: ii-V-I-VI7", 4, { {2, "m9"}, {7, "7b9"}, {0, "maj9"}, {9, "7"} } },
      { "Composers / Artists", "Debussy", "Debussy 1: SET-SET-SET-SET", 4, { {0, "aug"}, {2, "aug"}, {4, "aug"}, {6, "aug"} } },
      { "Composers / Artists", "Debussy", "Debussy 2: I-III-bVI-SET", 4, { {0, "6"}, {4, "6"}, {8, "6"}, {2, "quartal"} } },
      };
      count = (int)(sizeof(kAll) / sizeof(kAll[0]));
      return kAll;
   }

   // Semitones of a quality above its root (bit i = i semitones), 0 if unknown.
   inline unsigned Intervals(const char* q)
   {
      struct Q { const char* name; unsigned bits; };
      auto B = [](std::initializer_list<int> l) { unsigned b = 0; for (int i : l) b |= 1u << i; return b; };
      static const Q kQ[] = {
         { "M", B({0, 4, 7}) }, { "m", B({0, 3, 7}) }, { "5", B({0, 7, 12}) },
         { "7", B({0, 4, 7, 10}) }, { "maj7", B({0, 4, 7, 11}) }, { "m7", B({0, 3, 7, 10}) },
         { "9", B({0, 4, 7, 10, 14}) }, { "m9", B({0, 3, 7, 10, 14}) }, { "maj9", B({0, 4, 7, 11, 14}) },
         { "13", B({0, 4, 10, 14, 21}) }, { "7b9", B({0, 4, 7, 10, 13}) }, { "7#9", B({0, 4, 7, 10, 15}) },
         { "6", B({0, 4, 7, 9}) }, { "m6", B({0, 3, 7, 9}) }, { "6/9", B({0, 4, 7, 9, 14}) },
         { "add9", B({0, 4, 7, 14}) }, { "sus2", B({0, 2, 7}) }, { "sus4", B({0, 5, 7}) },
         { "aug", B({0, 4, 8}) }, { "dim7", B({0, 3, 6, 9}) }, { "m7b5", B({0, 3, 6, 10}) },
         { "m11", B({0, 3, 7, 10, 14, 17}) }, { "quartal", B({0, 5, 10, 15}) }, { "cluster", B({0, 1, 2, 3}) },
      };
      for (const Q& e : kQ)
      {
         const char* a = e.name; const char* b = q;
         while (*a && *a == *b) { ++a; ++b; }
         if (*a == 0 && *b == 0)
            return e.bits;
      }
      return B({0, 4, 7}); // unknown -> major triad
   }
}
