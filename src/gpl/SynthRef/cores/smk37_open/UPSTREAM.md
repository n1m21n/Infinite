# SMK37 OPEN upstream

Vendored by `scripts/synthref_vendor_mvave.py`; do not edit files under `upstream/`.

- Source: https://github.com/m-vave/smk37_open
- Commit: `b2d6bcbcd724adb13bcf85e44cfa16bb2c8153a8`
- Vendored: 2026-10-24
- Licences: GPL-3.0-only WITH GPL-3.0-linking-exception (app, web, sdk/include),
  Apache-2.0 (web/synth DX7 port of msfa, app/factory_dx7*.inc / fm_factory.inc), MIT (third_party/synth,
  web/synth sea_*). See the SPDX line of each file.
  See upstream/LICENSE.md and upstream/THIRD_PARTY.md.
- Not vendored: sdk/lib (proprietary JieLi engine library), sdk/firmware (stock images),
  sdk/include/libc, app/board (hardware drivers), tools.

86 files (SHA-256 prefix):

| File | SHA-256 |
|---|---|
| `app/audio_output.c` | `a3d932bf0479dc27` |
| `app/audio_output.h` | `38eb4301619684ff` |
| `app/clock_sync.h` | `81308a422cdb5158` |
| `app/factory_dx7.inc` | `e6e7cf416f58dd31` |
| `app/factory_dx7_packed.inc` | `37a36b66d62c9ed1` |
| `app/fm_factory.c` | `d50ad1100e96353f` |
| `app/fm_library.c` | `aea5d4c7de65cce1` |
| `app/fm_patch.h` | `23653c03cacf3fd9` |
| `app/instrument.h` | `691cfd3cfbf607ad` |
| `app/midi_router.c` | `58d008bf23453f8d` |
| `app/presets.c` | `7b8442692d62eee4` |
| `app/project.c` | `1b981957a7956819` |
| `app/samples.c` | `a199b04c254350c1` |
| `app/screen.c` | `33e3ca38f940e85f` |
| `app/smk37_controls.c` | `84631e8eb7e4e0e4` |
| `app/smk37_controls.h` | `f89372653d9f213d` |
| `app/synth/bass303.h` | `47e90ae5a34f679c` |
| `app/synth/drum808.h` | `e15b18600aefb1b7` |
| `app/synth/engine.h` | `34c09dcfec237e75` |
| `app/synth/fm1_param.h` | `8fe56e84bf360b04` |
| `app/synth/sampledrum.h` | `2f0f760c4761c71f` |
| `app/track_session.inc` | `2ed9db126adbce0a` |
| `app/transfer.c` | `814b6ca73fed96ed` |
| `app/tuning_hook.h` | `1c969685f6f4b4a7` |
| `app/version.h` | `8d152aecac00ab8a` |
| `web/engine/edit.c` | `806046f11c5bacb4` |
| `web/engine/effects.c` | `fbf2f5475925c757` |
| `web/engine/internal.h` | `709f13ed143619e7` |
| `web/engine/sequencer.c` | `8cd2df17ba968234` |
| `web/engine/Simulator.c` | `f6f49da5287c1aa9` |
| `web/engine/sound.c` | `132ac127628906db` |
| `web/synth/adsr.c` | `1b67517db3a908fe` |
| `web/synth/adsr.h` | `e50359a0ab971e01` |
| `web/synth/demo_bank.h` | `b0a7e8b1323b8330` |
| `web/synth/dx7core.c` | `ef5daf8a31ccaace` |
| `web/synth/dx7core.h` | `3dcee609f125a8e8` |
| `web/synth/dx7ctrl.h` | `673dcf6ffbbb75c4` |
| `web/synth/dx7env.c` | `056eebf3116cf43f` |
| `web/synth/dx7env.h` | `189ee063d4a5bd2a` |
| `web/synth/dx7lfo.c` | `283cdc5916c4725b` |
| `web/synth/dx7lfo.h` | `c200af6ce0089d86` |
| `web/synth/dx7math.c` | `69093924ded2ed22` |
| `web/synth/dx7math.h` | `03752d60d6ee9507` |
| `web/synth/dx7note.c` | `27f28fd09973d0c0` |
| `web/synth/dx7note.h` | `e50ee40d73eca4cf` |
| `web/synth/dx7pitchenv.c` | `32365a64bd39bd97` |
| `web/synth/dx7pitchenv.h` | `13fc9d1185d6a65d` |
| `web/synth/dx7synth.c` | `1491c759edf1f655` |
| `web/synth/dx7synth.h` | `5ea6e9aaded5e3cf` |
| `web/synth/fixed.c` | `8a4812571a9dc28d` |
| `web/synth/mixer.c` | `d9c4fa53a566ee67` |
| `web/synth/mixer.h` | `7fe9ebe30ad39189` |
| `web/synth/psg.c` | `2cdf5a5b993d44ab` |
| `web/synth/sample_metadata.inc` | `47cfaa9fc1062b9c` |
| `web/synth/sampledrum.c` | `dc720f9c171cca0a` |
| `web/synth/sea_decode.c` | `f869882efe51a02c` |
| `web/synth/sea_encode.h` | `73ceb8b74e21990d` |
| `web/synth/synth_dx7.h` | `7cc157025857ae42` |
| `web/synth/types.h` | `d6d0ba2f77ceb109` |
| `web/synth/voices.c` | `3a6cbfab27aa18e9` |
| `third_party/synth/bass303.c` | `edb988b6c3512a25` |
| `third_party/synth/bass303.h` | `82fb20bf30ce03a7` |
| `third_party/synth/bass303_state.h` | `bbc98118eb69fcac` |
| `third_party/synth/drum808.c` | `3fa331eac117d0c8` |
| `third_party/synth/drum808.h` | `4578387831fca632` |
| `third_party/synth/drum808_state.h` | `e33dd154f2991526` |
| `third_party/synth/dsp_math.h` | `30f887ba577e1c11` |
| `third_party/synth/emu2149.c` | `bd6f6eab1966ad83` |
| `third_party/synth/emu2149.h` | `4bfef8abef9ff40c` |
| `third_party/synth/engine.h` | `71d9d2816e335701` |
| `third_party/synth/fixed.h` | `dca8477d3be3a224` |
| `third_party/synth/pluck.c` | `fbffc0b950acef8e` |
| `third_party/synth/pluck.h` | `1275e911f8736d65` |
| `third_party/synth/psg.h` | `638ef873c6b631db` |
| `third_party/synth/sampledrum.h` | `4e7692b8e7dd9c76` |
| `third_party/synth/voices.h` | `4f704ebdcd9800a1` |
| `sdk/include/fm1.h` | `6a045469bfa41e82` |
| `sdk/include/fm1_audio_io.h` | `3c82758517f004ce` |
| `sdk/include/fm1_block.h` | `25be4238581552b1` |
| `sdk/include/fm1_device.h` | `d927c7cdc0d46cfc` |
| `sdk/include/fm1_engine_control.h` | `1ff4ec2534bd4653` |
| `sdk/include/fm1_sea.h` | `ced50bc6a08ba6e2` |
| `sdk/include/smk37_board.h` | `4350f5c7accad2c8` |
| `LICENSE.md` | `0c192c8d92ec61b4` |
| `THIRD_PARTY.md` | `69efb6a6d66c74a8` |
| `README.md` | `fa66c1cdcb376a30` |
