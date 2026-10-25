# Sloop SMK

Current release: **2.8.0-b3**, based on upstream Sloop 2.8, for **M-VAVE SMK-37 Pro & Elite**. Includes the new sequencer and editing screens, broader dual-core rendering, and paired blue/green/yellow/orange pad lights with white hit feedback and Low startup brightness. [Release notes and validation](docs/SLOOP_28_INTEGRATION.md).

This repository carries the Felucca SMK hardware foundation and Sloop musical/UI core. It is separate from [Felucca SMK](https://github.com/ChanceTheMaker/felucca-smk).

## What you can do

- Native Sloop sound engines, three synth parts and a drum track, six-operator FM, synth drums, sequencing, song/arranger pages, parameter locks, microtiming, fill conditions, insert effects and texture.
- SMK hardware adapters for the 37-key velocity keyboard, sixteen drum pads, eight encoders, four faders, pitch/mod wheels, sustain, display and indicator lights.
- Grouped Studio controls with per-section knobs/sliders, drag ordering, themes and native editor protocol.
- SMK simulator using the same musical and display source as the hardware candidate; color finishes, touch/mouse keys, full screen and a replayable tutorial.
- USB MIDI input in **both Browser Studio and Simulator**: notes, velocity, sustain, bend and modulation. Browser MIDI input does not request SysEx permission or install firmware.
- Browser sessions preserve virtual projects, user presets, four sample slots, synth drum kits, current FM6 voices and MIDI settings. Export a session file to keep an independent copy.
- The hardware candidate includes the bounded SMK USB updater. Its image and a return-to-Felucca 0.9.22 package pass the host loader test.

## Firmware status

The public beta is **2.8.0-b3** (`SMK-37_954`). The browser Studio and simulator use the matching 2.8 core. Pro installation/restart, sequencer and USB audio checks, CPU1 activity and isolated pad hits have version-specific evidence in the [integration notes](docs/SLOOP_28_INTEGRATION.md).

Pads repeat blue, green, yellow and orange in adjacent pairs on each row. Only the hit pad turns white. Startup brightness is Low unless a valid saved preference exists. A saved-settings write error remains unresolved; brightness persistence is not confirmed. Full 2.8 hardware save/reload and original-firmware round trip, Elite hardware and long-duration audio remain unverified. Earlier release tests are not fresh 2.8 hardware evidence.

[Restore original M-VAVE firmware](https://chancethemaker.github.io/sloop-smk/restore-stock/) remains available for Pro and Elite, with model-specific packages and the existing Windows native USB isolation. No Bluetooth support is included.

### Storage protection

Paired records hold projects, two banks of sixteen presets, settings, autosave
and FM6 data. Model/layout and ownership checks prevent silent replacement of
Felucca saves or unknown records. A failed project commit leaves the previous
RAM slot intact; Studio reports the device's save result. Older b2 replies do
not provide that result and are identified accordingly.

Host tests cover interrupted writes and protected areas. The owner's legacy
A-C project pairs were backed up and explicitly repaired during b5 testing;
that is a separate local operation, not automatic migration or a public upgrade
requirement. See [storage diagnostics](docs/SMK_STORAGE_DIAGNOSTICS.md).
Physical sample preparation is also explicit and never runs automatically at boot.

## Installation and availability

Open the [Sloop SMK installer](https://chancethemaker.github.io/sloop-smk/) in Chrome or Edge. The [Studio](https://chancethemaker.github.io/sloop-smk/editor.html) and [Simulator](https://chancethemaker.github.io/sloop-smk/simulator.html) also work without installing firmware.

1. Select **SMK-37 Pro** or **SMK-37 Elite**.
2. Choose **Automatic install / resume** for official 016 or an existing compatible Felucca, Sloop, Melodee or Doom SMK updater. Doom updater 007 or later is required. Older private builds need a separate migration.
3. The page downloads the checksum-verified official model-specific firmware and prepares b8-dc1 in the browser. An optional file selector is available if the automatic download fails.
4. Close Studio and other MIDI apps, connect the SMK directly by USB, confirm the beta notice, and click **Install SMK beta**. Keep USB and power connected until completion.

The public download contains the GPL application only. Official firmware is fetched from a pinned external mirror and processed locally; private device backups and calibration are not published. The portable updater retains model/layout validation and each instrument's calibration. It writes the application area; b8-dc1 runtime storage follows the separate ownership rules above.

The owner confirmed b8-dc1 FM6 sound and controls, normal USB installation and cold restart on the Pro. Earlier b5/b6 persistence and b7 display checks remain attributed to those builds. The public stock-install path is host-tested, not yet confirmed on hardware. Browser session exports are not device backups.

Cloning this repository includes the public app, Studio and Simulator. Running the preview server without a private candidate uses the public installer. A staged private candidate remains available only on localhost; add `?public` to preview the public path locally.

## Try locally

```powershell
python tools/serve_smk37.py --port 8774
```

- Studio: http://localhost:8774/editor.html
- Simulator: http://localhost:8774/simulator.html
- Homepage: http://localhost:8774/

Choose **Browser Studio** to enable audio, or open Simulator. If autoplay is blocked, press a key or Enable audio. To use a connected controller, choose **Enable USB MIDI input** and select its port. Device mode uses the hardware editor and requires a matching Sloop firmware installation.

Studio and Simulator use the same browser session, isolated from Felucca and the FM-1 Sloop website. Session files are tied to their browser runtime revision. The SMK panel is a firmware/UI simulation, not an emulation of the USB controller, flash silicon or Bluetooth radio.

Simulator shortcuts: **1-8** play the top pad row; **Shift+1-8** play the bottom row. Piano shortcuts are **A W S E D F T G Y H U J K O L P ;**; hold **Shift** to play an octave higher.

## SMK controls

| Control | Sloop mapping |
| --- | --- |
| Knobs 1-4, bank 1 | Master level, tempo, preset, track |
| Knobs 1-4, bank 2 | Track, engine, preset, page |
| Knobs 5-8 | Four native page parameters |
| Faders | Track levels; FADER BANK selects envelope ADSR |
| Pads | Sixteen drum lanes |
| PATCH / PARA / BT | Native HOME / SAVE / EDIT buttons |
| ARP and repeat / scale and chord | Native ARP / scale page buttons |
| GLO / FX / SEQ / PLAY / REC | Corresponding native Sloop controls |
| Pitch / mod / sustain | Expressive playing controls |

Native Sloop pages and hold gestures are retained where mapped. Hold PATCH for
settings; bank-1 K2 selects SCREEN / LIGHTS / AUDIO / SYSTEM. K5-K7 edit the
three rows; OCT- exits and saves. LIGHTS includes brightness and the individual
pad-color diagnostic; AUDIO includes TRS MIDI OUT. SYSTEM offers explicit user
sample preparation. Sample preparation is deferred for this test round.

## Build and test

The hardware build uses the existing Jieli toolchain with `JIELI_TOOLCHAIN` set to its root, Python dependencies and Pillow with libraqm:

```sh
# Current dual-core beta with guarded storage, samples and TRS output
python3 tools/build_sloop_smk.py --enable-storage --enable-samples --enable-trs --dual-core

# Browser engine
python3 tools/build_sloop_smk_browser.py --zig /path/to/zig
node web/audio/test_smk_engine.mjs
node web/audio/test_smk_worklet.mjs
node web/test_midi_input.mjs
node web/test_simulator_keys.mjs
node web/test_smk37_update.mjs
node web/test_smk37_public.mjs
sh tools/test_smk37_public.sh

# Candidate checks (Linux/WSL with a C compiler)
sh tools/test_sloop_smk_candidate.sh
python3 tests/test_sloop_smk_storage_reset.py
node web/test_project_save.mjs
node web/test_sample_capability.mjs
```

Zig 0.13.0 builds the WebAssembly runtime. Current hardware outputs are isolated in `build/sloop-smk-2.8.0-b3/`, with a manifest containing the version, hash, memory usage and enabled features. Build outputs and private packages stay under ignored `build/`. The raw application binary is **not** directly installable. Local package preparation is separate, and no build command accesses hardware.

See [port status and validation](docs/SLOOP_SMK_PORT.md) for the pinned upstream revision, artifact hashes, remaining work and test evidence. See [workflow rules](docs/UPSTREAM_WORKFLOW.md) before importing updates or publishing firmware.

## Remaining checks

- Physical sample upload and restart persistence remain deferred.
- Watch for stray pad LEDs; confirm modulation with a known responsive receiver patch.
- Elite first-install and exhaustive hardware coverage remain separate from the owner's Pro results.

Prior audio testing is retained; another long capture is not a routine acceptance
requirement without a relevant change or regression. See [work queue](docs/WORK_QUEUE.md).
Website publication and firmware release are separate: pushing source does not
promote a candidate to the installer.

## Source and project layout

Published source follows `integrate/sloop-2.6-smk` (the existing Pages branch); this release is tagged `sloop-smk-2.8.0-b3`. Musical upstream is [isod89/sloop-fm1](https://github.com/isod89/sloop-fm1), pinned to Sloop 2.8 commit `b77bf55e3bf804665b8f5dffb5955bd8443311f0`.

| Directory | Contents |
| --- | --- |
| `sloop/` | Vendored upstream musical/UI core and tools; local changes listed in `sloop/UPSTREAM.txt` |
| `firmware/` | SMK hardware adapters, guarded storage and USB updater |
| `web/` | Homepage, Studio, Simulator and browser audio runtime |
| `tools/` | Build tools, local preview server and packaging utilities |
| `tests/` | Host checks for hardware interfaces, storage and update boundaries |
| `docs/` | Port status, hardware evidence, workflow rules and remaining work |

## Credits and license

Sloop firmware by **isod89**, based on Felucca by **Leo Kuroshita (@kurogedelic), Hugelton Instruments**. SMK adaptation, Studio and simulator by **Chance Roth (@ChanceTheMaker)**.

GPL-3.0; retain upstream copyright notices, [LICENSE](LICENSE), [LICENSING.md](LICENSING.md) and [LICENSES](LICENSES). Vendored Sloop source lives under `sloop/`. Vendor firmware, calibration records and private device backups are not distributed here. M-VAVE, FM-1 and SMK-37 are trademarks of their respective owners.

## Windows USB installation

SMK-37 Pro installation and original-firmware restore use a local USB helper
on Windows, so Chrome does not own MIDI during the device restart. The same
Install and Restore buttons remain on the website. Other platforms and Elite
retain their existing browser installer; the native path is Pro-only.

With Python 3 and Node.js 22+ installed, run from this checkout:

```powershell
python tools/setup_native_smk.py
python tools/serve_native_smk.py --allow-hosted
```

Then use the Sloop website, or `http://localhost:8892/`. Allow local-network
access when prompted. The setup command verifies the official Pro 016 package
and the published Sloop 2.8.0-b3 app; it never accesses MIDI or installs firmware.
Close connected Studio/Simulator tabs and other MIDI apps before installing.
If Chrome is already frozen, restart it first to release its previous session.
Keep the helper running until the installer confirms the returned identity.
No browser flags or alternative browser profile are required.


## SMK flash compatibility release

The SMK update and stock-restore path now accepts the reported EB6014 flash
chip alongside 856014 and displays detailed loader failures. Update older
Windows USB-helper checkouts before installing this release.
See [release scope, evidence and recovery limits](docs/SMK_FLASH_COMPATIBILITY.md).
