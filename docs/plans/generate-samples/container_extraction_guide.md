# Proprietary Audio & Instrument Container Extraction Guide (`WD-Black`)

This document records the exact binary container layouts, keys, and extraction scripts used to make packed instrument libraries (`Spectrasonics STEAM`, `Spitfire Audio LABS`, `Kontakt / Native Instruments`, and other major plugins/DAWs on `/Volumes/WD-Black`) reachable for training the Infinite Sample Generator (`local` scope).

---

## 1. Final Extracted Data & Tools

### Scripts Created
- [`tools/universe/steam_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/steam_reader.py): Deep extractor for Spectrasonics (`Omnisphere`, `Keyscape`, `Trilian`, `Sonic Extensions`) `.db`, `.db2`, `.omnisphere`, and `zmap.index` containers.
- [`tools/universe/spitfire_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/spitfire_reader.py): Deep extractor for Spitfire Audio `LABS` (`.spitfire` + SQLite `.db`) and Kontakt `.nkc` catalogs (`Spitfire Audio Olafur Arnalds Stratus`, `Kontakt Factory Library`, `Noire`, `Output Exhale`, etc.).
- Existing companion readers in `tools/universe/`: [`preset_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/preset_reader.py), [`exs_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/exs_reader.py), [`als_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/als_reader.py), [`audio_features.py`](file:///Users/namansoni/Infinite/repo/tools/universe/audio_features.py).

### Output Datasets (`~/Infinite/training-data/`)
| File | Rows | Content |
|---|---:|---|
| `steam/arp_and_mods.jsonl` | **13,935** | **11,029** `<ARPSEQ2>` rhythmic step sequences (step timing, gate, velocity, slice/transpose, swing, BPM, meter) + **13,770** `<MOD_MATRIX>` routings + sound-designer `Genre`/`Mood`/`Type` tags |
| `steam/soundsources.jsonl` | **11,519** | All `zmap.index` multisample Soundsource instruments with human-labeled `Mood`, `Timbre`, `Type`, `Technique`, `Complexity`, `Source`, and `Description` |
| `steam/wavetables.jsonl` | **2,495** | Raw `float32` wavetables (`.stmwf`) from `~BundleArchives.db`/`.db2` analyzed into 32-band spectra, centroid, rolloff, flatness, crest, and RMS |
| `steam/omnisphere_packs.jsonl` | **354** | Full synth/FX knob presets extracted from third-party `.omnisphere` monolith packages (`Luftrum Vangelis`, `Triple Spiral Rust Extended`, `Tom Wolfe Sleep`) |
| `steam/soundsource_audio.jsonl` | **1,100+** | In-memory decoded `RIFF` and `SpCA` Soundsource `.wav` streams analyzed into 32-band spectra, timbre, stereo correlation, and 10 ms attack/decay envelopes |
| `spitfire/labs_audio.jsonl` | **1,998** | Decoded `SFLC` audio feature rows across 142 `.spitfire` containers in all 21 `LABS` libraries (`keys`, `synth`, `pad`, `pluck`, `strings`, `brass`, `bass`, `chord`) |
| `spitfire/nkc_catalog.jsonl` | **229** | Multisample instrument zone/velocity/round-robin catalogs extracted from 254 `.nkc` files (`Olafur Arnalds Stratus`, `Kontakt Factory`, `Cuba`, `Noire`, `Exhale`, `Omnia`) |

---

## 2. Exact Technique: How Each Container Was Solved

### A. General 4-Step Methodology (Applicable to Any Plugin or DAW Container)
1. **Header & Entropy Triage (`file` + first 512 bytes in hex/ASCII)**:
   - Separate containers into **(a) plain-text / XML / JSON / SQLite / zlib** headers vs. **(b) obfuscated payload streams**. Most commercial audio plugins leave their directory tables, preset trees, and stream headers in plain text or standard SQLite/XML so their browser UI can search 10,000+ patches in milliseconds without decrypting gigabytes of audio.
2. **Known-Plaintext XOR Recovery on Audio Headers & Silence**:
   - Uncompressed or lossless audio containers (`WAV`, `FLAC`, `AIFF`, custom LPC/Rice blocks) have known magic bytes (`RIFF....WAVEfmt `, `fLaC`, `SFLC`) and known near-zero sample regions (`0x000000` or `0xffffff` in two's complement PCM during pre-attack silence).
   - XORing the first 16–64 bytes of the stream against the known format header (from the unencrypted index table) or inspecting repeating 4-byte / 16-byte patterns in silent regions immediately reveals fixed or header-derived XOR masks.
3. **Seek-Based In-Memory Streaming (Zero Disk Duplication)**:
   - Parse the container's directory table once (`<FileSystem>` XML, `.nkc` UTF-16 table, or SQLite `.db`), `seek()` directly to `base_offset + entry_offset`, decode only the first `N` seconds in RAM, and compute summary statistics (`32-band spectrum`, `centroid`, `rolloff`, `flatness`, `crest`, `attack_ms`, `decay_ms`, `env`). No `.wav` files are ever written to disk.

---

### B. Spectrasonics (`Omnisphere`, `Keyscape`, `Trilian`, `Sonic Extensions`)

#### 1. `.db`, `.db2`, and `.omnisphere` VFS Container Layout
Every Spectrasonics archive begins with an unencrypted `<FileSystem>` XML tree and ends with `</FileSystem>\n`. Immediately after that newline (`base = raw.find(b"</FileSystem>") + 14`), all files sit concatenated back-to-back at `base + int(offset)` of length `int(size)`:
```xml
<FileSystem>
  <DIR name="ARP + BPM">
    <FILE name="Bandito Heist.prt_omn" offset="0" size="45229"/>
  </DIR>
</FileSystem>
```

#### 2. `.prt_omn` / `.mlt_omn` Patches (Hex-Encoded IEEE-754 Floats)
- Every patch is plain XML (`<AmberPart><SynthEngine>...`).
- All numeric attributes are **big-endian IEEE-754 32-bit floats written as 8 hex characters**:
  ```python
  val = struct.unpack(">f", bytes.fromhex(hex_str.rjust(8, "0")))[0]
  # e.g. "3f800000" -> 1.0, "3f000000" -> 0.5, "42f00000" -> 120.0 BPM
  ```
- **Arpeggiator Grooves**: Stored in `<ARPSEQ2 TEMPO="..." TICKSPERQUARTER="600">` with `<SLICESEQSTEP BEGIN="..." END="..." VEL="..." SLICEINDEX="..." TRANSPOSE="..."/>`.
- **Modulation Matrix**: Stored in `<MOD_MATRIX source0="Wheel" target0="D freq" lo0="0" hi0="3f000000" defV0="3e051eb9" mute0="0"/>`.
- **Human Labels**: Stored in `<ENTRYDESCR ATTRIB_VALUE_DATA="Genre=...;Mood=...;Type=...;Complexity=...;"/>`.

#### 3. `.stmwf` Wavetables (`Wavetables/~BundleArchives.db` & `.db2`)
- Stored as **unencrypted little-endian 32-bit IEEE-754 float arrays** (`np.frombuffer(raw, dtype=np.float32)`), typically `1,048,576` bytes (`262,144` floats) per wavetable.

#### 4. Embedded Soundsource Audio — Format 1: Obfuscated `RIFF....WAVE`
- Found in `Omnisphere Core Library` (33,084 files) and `.omnisphere` expansions.
- In the `fmt ` chunk:
  - `wFormatTag = 1` (PCM), `sampleRate = 44100`, `bitsPerSample = 16` or `24`, `blockAlign = 4` or `6`.
  - Spectrasonics adds `+20` to `nChannels` (writing `22` instead of `2`) so standard WAV players reject the header. True channels = `blockAlign // (bitsPerSample // 8)`.
- In the `data` chunk:
  - Every 4 bytes of the PCM `data` payload are XORed with the fixed 4-byte mask **`7d b5 7e 21`** (note that `-1` in two's complement `ff ff ff ff` XORed with `7d b5 7e 21` appears as `82 4a 81 de` in silent regions):
  ```python
  XOR4_WAV = np.array([0x7d, 0xb5, 0x7e, 0x21], dtype=np.uint8)
  dec = raw_data_bytes ^ np.tile(XOR4_WAV, len(raw_data_bytes) // 4)
  ```

#### 5. Embedded Soundsource Audio — Format 2: `SpCA` (`Keyscape`, `Trilian`, `Omnisphere 2`)
- Found in 200,000+ `.wav`-named entries inside `Keyscape`, `Trilian`, `Sonic Extensions`, and newer `Omnisphere` `.db` files.
- Starts with magic bytes `b"SpCA"` instead of `b"fLaC"`, followed by standard FLAC metadata blocks (`STREAMINFO` type `0`, `VORBIS_COMMENT` type `4` containing `reference libFLAC 1.2.1` and `STINFO=0 0 0 4`, `SEEKTABLE` type `3`, `PADDING` type `1`).
- After the last FLAC metadata block (`hdr >> 7 == 1`, ending at byte `pos`), the first **4 bytes** (`pos .. pos+4`, which hold the first FLAC frame header `ff f8 c9 ac`) are unencrypted, and **all remaining bytes from `pos + 4` to EOF are XORed with the 4-byte repeating key `bc ef 42 12`**:
  ```python
  XOR4_SPCA = np.array([0xbc, 0xef, 0x42, 0x12], dtype=np.uint8)
  tail = np.frombuffer(w_bytes, dtype=np.uint8, count=rem, offset=pos + 4) ^ np.tile(XOR4_SPCA, rem // 4)
  valid_flac = b"fLaC" + w_bytes[4:pos + 4] + tail.tobytes()
  ```

---

### C. Spitfire Audio (`LABS` — 21 Libraries, 148 `.spitfire` Containers)

#### 1. `.spitfire` Container Header & Unencrypted Metadata Table
- Bytes `0..16`: `<8sII` -> `b"Spitfire"`, `version = 1`, `sample_count = N`.
- Bytes `16 .. 16 + N*32`: `N` 32-byte directory entries (`<8I`):
  `(file_id, flags, audio_byte_len, meta_byte_len, audio_offset, _, meta_offset, _)`.
- At `meta_offset` (`40` bytes per sample), the sample's format descriptor is **unencrypted**:
  `fmt_id, bytes_per_sample, channels, sample_rate, total_frames = struct.unpack("<IIIII", b[moff:moff+20])`.

#### 2. Per-Sample 16-Byte XOR Key Recovery & `SFLC` (`ABLK`/`DATA`) Stream
- Every sample stream at `audio_offset` begins with a 24-byte `SFLC` header whose first 16 bytes in plaintext are deterministically known from the unencrypted metadata at `moff`:
  ```python
  exp16 = struct.pack("<4sIHHI", b"SFLC", 0x00010000, bps * 8, ch, sr)
  key16 = bytes(x ^ y for x, y in zip(b[aoff:aoff + 16], exp16))
  ```
- XORing the sample stream at `aoff` with `key16` (`raw[i] ^ key16[i % 16]`) reveals a sequence of `ABLK` chunks (`1024` frames = `21.33 ms` per block at `48 kHz`), each containing `channels` `DATA` sub-chunks:
  - `DATA` header (`44` bytes): `b"DATA"`, `uint32 data_size`, `uint32 flags` (where `k = flags >> 24` is the Rice parameter), **4 signed 32-bit fixed-point LPC coefficients `(a1, a2, a3, a4)` scaled by `1/256`**, and **4 uncompressed 24-bit signed PCM warm-up samples `(x0, x1, x2, x3)`** (`(a1*x3 + a2*x2 + a3*x1 + a4*x0) >> 8 == x4_hat`).
  - Evaluating the 4th-order all-pole transfer function $H(e^{j\omega}) = 1 / (1 - \sum_{m=1}^4 (a_m/256) r^m e^{-jm\omega})$ weighted by the block's true PCM power $\frac{1}{4}\sum_{i=0}^3 x_i^2$ yields the 32-band spectrum, centroid, rolloff, flatness, RMS envelope, and stereo correlation in milliseconds without unpacking the Rice residuals.

---

## 3. How to Extract Every Other Instrument, Plugin & DAW on `WD-Black`

| Instrument / DAW on `WD-Black` | Container Extensions | How It Is Structured & How to Extract |
|---|---|---|
| **Native Instruments Kontakt** (`Kontakt Factory Library`, `Noire`, `Output Exhale`, `Omnia Choir`, `Cuba`, `Saffron`, `Sequis`, `Sunburst`, `Olafur Arnalds Stratus`) | `.nki`, `.nkm`, `.nksn`, `.nka`, `.nkc`, `.nkx`, `.ncw` | **Already wired in `spitfire_reader.py`**: `.nkc` files contain unencrypted UTF-16LE sample filenames (`Note`, `Vel`, `RR`, `Articulation`). `.nka` files (in `Exhale`, `Cuba`, `Sequis`, `Saffron`, `Sunburst`) are plain-text ASCII parameter/sequencer array tables! Unencrypted `.ncw` files (e.g. `Stratus/Samples/IRs/*.ncw`) are standard NI lossless audio decodable or inspectable via header, and encrypted `.nkx` libraries can be rendered headlessly via Kontakt VST3/AU. |
| **Logic Pro / Alchemy / EXS24** (`Library/Application Support/Logic`, `Apple Loops`) | `.exs`, `.acp`, `.pst`, `.caf` | **Already wired**: [`exs_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/exs_reader.py) reads `.exs` binary zone/sample chunks (`JBOS`/`TBOS`); `alchemy.jsonl` (`3,702` rows) and [`preset_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/preset_reader.py) read `.acp` and `EMAGPPST` `.pst` float arrays; `.caf` Apple Loops contain both audio (`audio_features.py`) and embedded MIDI/transient chunks. |
| **Ableton Live 12 Suite & Packs** (`Ableton Live Suite 12 Library Packs`) | `.als`, `.adv`, `.adg`, `.agr`, `.alp` | **Already wired**: `.als`, `.adv`, and `.adg` are **gzip-compressed UTF-8 XML** (`gzip.open(path)`). [`als_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/als_reader.py) extracts arrangements, MIDI clips, and automation; [`preset_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/preset_reader.py) extracts all `<Manual Value="..."/>` device knobs. |
| **u-he (`Diva`, `Hive`, `Zebra`, `Repro`)** (`PML Diva Presets`) | `.h2p`, `.h2pv` | **Already wired in [`preset_reader.py`](file:///Users/namansoni/Infinite/repo/tools/universe/preset_reader.py)**: Plain text with `/*@Meta*/` tags followed by `#cm=MODULE` `key=value` knob pairs. |
| **Serum 2 / Vital / Phase Plant / Kilohearts** | `.vital`, `.serum`, `.fxp`, `.phaseplant`, `.ksd` | `.vital` is **plain JSON** (contains full synth knob values, LFO shapes, mod matrix, and base64 float wavetables). `.phaseplant` / `.multipass` / `.snapheap` are zip/binary chunks with plain JSON/XML parameter trees. |
| **Audiomodern `Soundbox`** (`Sample Library Soundbox`, `Berlin Deep House 1 & 2`) | `.wav`, `.aif`, `.sbpack`, `.pbpack` | `Sample Library Soundbox` and `Berlin Deep House` already store **2,927 plain `.wav`/`.aif` files** (11.5 GB) directly on disk; `.sbpack`/`.pbpack` are standard ZIP/container archives with XML/JSON zone maps + audio. |
| **Bitwig Studio** (`Bitwig Studio`) | `.bwproject`, `.bwpreset`, `.multisample` | `.multisample` and `.bwproject` are **standard ZIP archives** (`zipfile.ZipFile`) containing `multisample.xml` (exact key/velocity/loop zone maps) and embedded `.wav` samples; `.bwpreset` has a plain header + structured parameter map. |
| **Toontrack `EZkeys 2` & `XLN Audio`** | `EZkeys 2/MIDI`, `.obw`, `.xpak` | `EZkeys 2` MIDI packs contain thousands of categorized chord/piano `.mid` files; `.obw` and `.xpak` use a file-table header + packed audio/MIDI stream (or can be batch-rendered through the VST3/AU plugin). |
| **Headless VST3/AU Render Fallback (Any Encrypted Synth/Sampler)** | `.vst3`, `.component` | For any instrument whose sample stream uses strong encryption (e.g. commercial Kontakt `.nkx`, `.xpak`), load the installed `.vst3`/`.component` in a headless host, send MIDI notes (`C2, C3, C4` at velocities `40, 90, 120`), and pipe the rendered float buffer into `steam_reader.analyse_pcm_array()` — storing only the 32-band summary and envelope (`local only`). |

---

## 4. Commands to Re-Run or Expand Extraction

```bash
# 1. Extract Spitfire LABS audio features (all 21 libraries) + Kontakt .nkc catalogs
#    Use --per-container 0 to extract all 11,450 LABS samples instead of 24 per container
python3 ~/Infinite/repo/tools/universe/spitfire_reader.py /Volumes/WD-Black --per-container 24

# 2. Extract Spectrasonics Wavetables, ARP grooves, Mod Matrices, .omnisphere packs, zmap.index & Soundsource audio
#    Use --wavs-per-db 20 (or 0 for all) to increase decoded Soundsource audio rows
python3 ~/Infinite/repo/tools/universe/steam_reader.py /Volumes/WD-Black --wavs-per-db 6
```
